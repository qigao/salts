#ifndef CFLOW_CNET_DOMAIN_ROUTE_H
#define CFLOW_CNET_DOMAIN_ROUTE_H

#include <cflow/actor.h>
#include <cnet/cnet.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Optional per-source-connection / per-target-domain-Actor cross-owner route.
 * Link Salts::CFlowCNet. The target owner initializes/destroys this handle.
 * One nominated CNet connection owner binds once and is the sole producer.
 * Many independent routes may target one Actor on one owner thread.
 *
 * The route stores bounded retained PAYLOAD SLOTS, not a second task/event
 * queue. A producer copies a borrowed CNet receive view into one preallocated
 * slot, then submits a trivially copied delivery token directly to the
 * existing Actor MPMC Mailbox. Business execution, borrow and ACK belong to
 * the target owner. No worker, NativeIO backend, CNet client/manager, poll,
 * cross-owner forwarding thread or transport terminal is created.
 *
 * Control: init/bind must be externally serialized. After source binding,
 * producer operations and target operations may overlap; access is protected
 * by one bounded per-route mutex. Host must stop/join source producers and
 * quiesce target Actor callbacks before target-side destroy.
 */
typedef struct cflow_cnet_domain_route {
    void *impl;
} cflow_cnet_domain_route;

/** Opaque generation-safe routing ticket; not a payload reference. */
typedef struct cflow_cnet_domain_route_credit {
    uint64_t incarnation;
    uint64_t generation;
    uint32_t slot;
    uint32_t source_owner;
    cnet_connection connection;
} cflow_cnet_domain_route_credit;

/** CMeta-typed Actor event. No raw CNet callback pointer or buffer reference. */
typedef struct cflow_cnet_domain_route_delivery {
    uint64_t incarnation;
    uint64_t generation;
    uint32_t slot;
    uint32_t source_owner;
    cnet_connection connection;
    size_t size;
    cnet_message_kind kind;
} cflow_cnet_domain_route_delivery;

extern const cmeta_type_desc cflow_cnet_domain_route_delivery_type;

typedef struct cflow_cnet_domain_route_config {
    /** Retained Actor producer ref; route does not own Actor lifecycle. */
    const cflow_actor_ref *actor;
    /** Actor's Machine event schema must declare exactly the descriptor above. */
    cflow_event_id event_id;
    /** Selected source Owner identifier, not a global scheduling hint. */
    uint32_t source_owner;
    /** Original source CNet connection generation, never migrated. */
    cnet_connection connection;
    /** Fixed retained payload slots on the target Owner. */
    size_t slot_capacity;
    /** Hard maximum of one CNet receive chunk. */
    size_t max_receive_bytes;
} cflow_cnet_domain_route_config;

typedef struct cflow_cnet_domain_route_stats {
    size_t slot_capacity;
    size_t active_slots;
    size_t awaiting_ack;
    size_t staged;
    size_t retained_bytes;
    size_t peak_retained_bytes;
    uint64_t reserved_credits;
    uint64_t rollback_credits;
    uint64_t received_values;
    uint64_t actor_accepted;
    uint64_t actor_full;
    uint64_t acknowledged;
    uint64_t abandoned;
    int fatal_status;
    bool source_bound;
    bool receive_credit_live;
    bool sealed;
    bool source_terminal;
} cflow_cnet_domain_route_stats;

/** Target Owner: initialize one route and all payload slots up front.
 * Source owner must later explicitly bind from its final CNet owner thread.
 * The input cflow_actor_ref remains caller-owned. Failure leaves handle zero.
 */
int cflow_cnet_domain_route_init(
    cflow_cnet_domain_route *route,
    const cflow_cnet_domain_route_config *config);

/** Source Owner: record this exact thread once; cannot rebind or migrate. */
int cflow_cnet_domain_route_bind_source(cflow_cnet_domain_route *route);

/** Source Owner: retrieve the route's original immutable source-owner and
 * connection identity. Fails EPERM from target/other threads. Useful for
 * binding one held CNetManager attachment to several semantic group routes,
 * without using a connection pointer as a business payload lifetime token. */
int cflow_cnet_domain_route_get_source_binding(
    cflow_cnet_domain_route *route,
    cnet_connection *out_connection,
    uint32_t *out_source_owner);

/** Source Owner: reserve exactly one credit BEFORE host cnet_receive(1).
 * A second credit or pending Mailbox-FULL staging returns EBUSY; all payload
 * slots leased by Actor yields ENOBUFS. No IO request is made by the route.
 * Source_owner is fixed at route init. A credit must be rolled back after
 * cnet_receive rejection, or settled by one real on_receive / CNet terminal.
 */
int cflow_cnet_domain_route_reserve(
    cflow_cnet_domain_route *route,
    cflow_cnet_domain_route_credit *out);

/** Source Owner: rollback ONLY when host cnet_receive refused its demand.
 * Never cancel a receive already accepted by CNet with this operation. */
int cflow_cnet_domain_route_cancel_credit(
    cflow_cnet_domain_route *route,
    cflow_cnet_domain_route_credit credit);

/** Source Owner on the exact CNet on_receive callback. Consumes credit once,
 * copies borrowed callback bytes into target-owned fixed storage and tries one
 * Actor admission without inserting into another queue. Actor FULL returns
 * ENOBUFS with bytes retained; the target may try retry_staged once per quantum.
 * Oversize/type violation seals this route (not neighboring connections).
 */
int cflow_cnet_domain_route_receive(
    cflow_cnet_domain_route *route,
    cflow_cnet_domain_route_credit credit,
    const cnet_receive_view *view);

/** Target Owner: try to forward at most the one staged FULL event.
 * Accepted work increments out_work; FULL does not drop bytes or spin. */
int cflow_cnet_domain_route_retry_staged(
    cflow_cnet_domain_route *route,
    size_t *out_work);

/** Target Owner: borrow immutable preallocated bytes of a delivered event.
 * Valid until the matching ACK, explicit post-quiescence abort or destruction.
 * This pointer is NEVER a CNet receive callback borrowed pointer. */
int cflow_cnet_domain_route_borrow(
    cflow_cnet_domain_route *route,
    const cflow_cnet_domain_route_delivery *delivery,
    cnet_receive_view *out);

/** Target Owner: business processing ACK, once. Actor Mailbox acceptance and
 * CNet native completion NEVER imply this ACK. Returns ENOENT for a stale,
 * duplicate, foreign-generation, already ACKed or malformed delivery. */
int cflow_cnet_domain_route_acknowledge(
    cflow_cnet_domain_route *route,
    const cflow_cnet_domain_route_delivery *delivery);

/** Source Owner: authoritative CNet CLOSED/FAILED. Seals further demand,
 * retires any still-outstanding receive credit, and keeps staged/accepted
 * business payload slots until target ACK or post-quiescence abort. */
int cflow_cnet_domain_route_source_terminal(cflow_cnet_domain_route *route);

/** Source Owner: seal demand, without closing CNet or discarding payloads.
 * The host must separately ensure CNet callback and producer quiescence. */
int cflow_cnet_domain_route_seal(cflow_cnet_domain_route *route);

/** Target Owner: after source producers and Actor callbacks have actually
 * quiesced, explicitly discard any unacknowledged retained events (including
 * events removed by Actor Mailbox cancel). Requires sealed and no credit. */
int cflow_cnet_domain_route_abort_after_quiescence(
    cflow_cnet_domain_route *route);

/** Thread-safe, coherent snapshot. No progress or lifetime pin is implied. */
int cflow_cnet_domain_route_get_stats(
    cflow_cnet_domain_route *route,
    cflow_cnet_domain_route_stats *out);

/** Target Owner: requires seal + zero active slots and external producer /
 * Actor callback quiescence. Releases retained producer ref and storage only;
 * never destroys CNet, NativeIO, the Actor or either host Owner. */
int cflow_cnet_domain_route_destroy(cflow_cnet_domain_route *route);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_CNET_DOMAIN_ROUTE_H */
