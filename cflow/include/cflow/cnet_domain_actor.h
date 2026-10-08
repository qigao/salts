#ifndef CFLOW_CNET_DOMAIN_ACTOR_H
#define CFLOW_CNET_DOMAIN_ACTOR_H

#include <cflow/actor.h>
#include <cnet/cnet.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Optional owner-local CNet receive-credit -> Domain Actor staging bridge.
 * Link Salts::CFlowCNet. This is NOT the existing CFlow IO Actor adapter:
 * it publishes typed semantic events into a Machine-backed domain Actor.
 *
 * No CNet client, observer, connection, poll, worker, event queue, or Actor is
 * created here. The host owns its CNet receive/terminal callbacks and forwards
 * the already borrowed receive view. All receive demand for the bridged
 * connection must follow reserve_credit -> cnet_receive(1) -> callback (or
 * rollback on rejection); never issue another raw cnet_receive outside this
 * single-credit contract. All operations below are single-owner.
 */
typedef struct cflow_cnet_domain_bridge {
    void *impl;
} cflow_cnet_domain_bridge;

/* An immutable, trivially copied Actor Mailbox envelope. It is NOT a payload
 * reference: the matching bridge owns bytes until explicit acknowledge or
 * the host's post-quiescence abort. Never dereference data from this token. */
typedef struct cflow_cnet_domain_delivery {
    uint64_t incarnation;
    uint64_t generation;
    uint32_t slot;
    cnet_connection connection;
    size_t size;
    cnet_message_kind kind;
} cflow_cnet_domain_delivery;

extern const cmeta_type_desc cflow_cnet_domain_delivery_type;

/* One tentative CNet receive-credit reservation (no cnet_receive is issued
 * by the bridge). The host must call cnet_receive(client, connection, 1)
 * exactly once after reserve succeeds, and cancel_credit on rejection.
 * At most one credit may be outstanding on this bridge/connection. */
typedef struct cflow_cnet_domain_credit {
    uint64_t incarnation;
    uint64_t generation;
    uint32_t slot;
} cflow_cnet_domain_credit;

typedef struct cflow_cnet_domain_config {
    /** Retained by the bridge at init; the input ref remains caller-owned. */
    const cflow_actor_ref *actor;
    /** The Actor's already declared Event ID; its payload type MUST equal
     * cflow_cnet_domain_delivery_type. Mismatches are hard errors. */
    cflow_event_id event_id;
    /** Stable CNet session generation; no implicit connection migration. */
    cnet_connection connection;
    /** Fixed payload lease slots, positive and <= UINT32_MAX. */
    size_t slot_capacity;
    /** Exact maximum size of ONE borrowed CNet receive callback. */
    size_t max_receive_bytes;
} cflow_cnet_domain_config;

typedef struct cflow_cnet_domain_stats {
    size_t slot_capacity;
    size_t max_receive_bytes;
    size_t active_slots;
    size_t awaiting_ack;
    size_t pending_actor_admission;
    size_t retained_bytes;
    size_t peak_retained_bytes;
    uint64_t reserved_credits;
    uint64_t rolled_back_credits;
    uint64_t received_views;
    uint64_t mailbox_accepted;
    uint64_t mailbox_full;
    uint64_t acknowledged;
    uint64_t abandoned;
    int fatal_status;
    bool credit_outstanding;
    bool sealed;
    bool transport_terminal;
} cflow_cnet_domain_stats;

/** Initialize bounded owned payload storage and retain one Actor producer ref.
 * Failure leaves a zero wrapper. Actor must be started before receive events
 * are completed; its output/lifetime is still owned by the application. */
int cflow_cnet_domain_bridge_init(
    cflow_cnet_domain_bridge *bridge,
    const cflow_cnet_domain_config *config);

/** Reserve one payload slot BEFORE authorizing the host's next CNet receive
 * demand. This is a tentative operation: on CNet admission failure call
 * cancel_credit with the exact returned token (no bytes were transferred).
 * When all slots are awaiting Actor acknowledgement return ENOBUFS; while
 * another credit/staged callback is outstanding return EBUSY.
 */
int cflow_cnet_domain_reserve_credit(
    cflow_cnet_domain_bridge *bridge,
    cflow_cnet_domain_credit *out);

/** Return a still-outstanding tentative credit after CNet receive rejection.
 * The host must not use this to cancel a receive already accepted by CNet.
 * After accepted CNet demand, only its real callback or transport terminal
 * settles the credit. */
int cflow_cnet_domain_cancel_credit(
    cflow_cnet_domain_bridge *bridge,
    cflow_cnet_domain_credit credit);

/** Handle exactly one already authorized CNet callback with a borrowed view.
 * Copies bytes into a preallocated slot before sending a trivial envelope.
 * ACCEPTED retains the bytes until application ACK; Actor FULL keeps the slot
 * in bounded staging, pauses further receive-credit admission and returns
 * ENOBUFS (retry later through retry_actor). NO dropped/overwritten bytes.
 *
 * An unsolicited callback for the bound connection with no reserved credit,
 * oversized payload, invalid kind or stale Actor status permanently seals the
 * bridge with an explicit error. A foreign connection identity returns ENOENT
 * without consuming the bound connection's credit. The CNet connection remains
 * host-owned; host must close it by its protocol policy, not infer delivery.
 */
int cflow_cnet_domain_receive(
    cflow_cnet_domain_bridge *bridge,
    cnet_connection connection,
    const cnet_receive_view *view);

/** Try the one staged event again (never spin or allocate). FULL stays bounded;
 * zero work is a valid idle result. The host drives this after Actor progress.
 * Does not poll CNet, issue receive demand or run the Actor Executor. */
int cflow_cnet_domain_retry_actor(
    cflow_cnet_domain_bridge *bridge, size_t *out_work);

/** Borrow retained bytes during application processing of a delivered event.
 * On success out points to bridge-owned storage until matching acknowledge,
 * abort_after_quiescence, or bridge destroy. The borrowed callback buffer
 * originally received from CNet is never retained. */
int cflow_cnet_domain_borrow(
    cflow_cnet_domain_bridge *bridge,
    const cflow_cnet_domain_delivery *delivery,
    cnet_receive_view *out);

/** Exactly once acknowledgement when the APPLICATION has consumed/settled the
 * event. Actor Mailbox acceptance is NOT this acknowledgement. A copied
 * envelope never extends payload lifetime after this call. */
int cflow_cnet_domain_acknowledge(
    cflow_cnet_domain_bridge *bridge,
    const cflow_cnet_domain_delivery *delivery);

/** Seal new receive-credit reservations, without closing the CNet session or
 * prematurely discarding staged/accepted Actor events. */
int cflow_cnet_domain_seal(cflow_cnet_domain_bridge *bridge);

/** Observe authoritative CNet CLOSED/FAILED; retires any admitted-but-unused
 * receive credit, keeping already received bytes and delivered lease tokens.
 * Must be invoked only after CNet confirms no later receive callback. */
int cflow_cnet_domain_transport_terminal(cflow_cnet_domain_bridge *bridge);

/** Explicit host-only abort after the Actor's callbacks, producer admission
 * and CNet receive callbacks have all quiesced. This is the **only** way to
 * reclaim events silently discarded by Actor Mailbox cancel. Requires sealed
 * bridge and no outstanding CNet credit; normal in-flight processing should
 * instead call acknowledge. Not a mechanism for fabricating Actor completion. */
int cflow_cnet_domain_abort_after_quiescence(
    cflow_cnet_domain_bridge *bridge);

/** Coherent owner-only snapshot; does not claim I/O progress or processing. */
int cflow_cnet_domain_get_stats(
    cflow_cnet_domain_bridge *bridge,
    cflow_cnet_domain_stats *out);

/** Owner-only destroy requires seal, zero active credits/staged/leased events,
 * and host-established callback/producers quiescence. EBUSY otherwise.
 * Releases the bridge's retained Actor producer ref but never destroys Actor,
 * CNet client/connection, or shared executor. */
int cflow_cnet_domain_bridge_destroy(cflow_cnet_domain_bridge *bridge);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_CNET_DOMAIN_ACTOR_H */
