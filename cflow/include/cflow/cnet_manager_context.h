#ifndef CFLOW_CNET_MANAGER_CONTEXT_H
#define CFLOW_CNET_MANAGER_CONTEXT_H

#include <cflow/cnet_domain_route.h>
#include <cnet/manager.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Optional per-managed-connection cross-owner business context hold.
 * Link Salts::CFlowCNetManager, not the lean Salts::CFlowCNet target.
 *
 * The CNet SOURCE owner creates this after cnet_manager_connect/adopt and
 * cflow_cnet_domain_route_bind_source, while the selected manager attachment
 * has hold_context=true. Multiple semantic Actor routes may use ONE managed
 * connection. The object owns a fixed copy of route pointers, but neither
 * the manager's context, routes, CNet connection nor Actor.
 *
 * Source-side poll_release authoritatively checks the manager's RETIRED
 * state and EVERY route's real completion/ACK obligations before calling
 * cnet_manager_release_context EXACTLY ONCE. CNet terminal alone cannot
 * release an unacknowledged payload or imply a business/WAL commit.
 *
 * The destination Owner calls notify_settled AFTER ACK or a valid
 * abort_after_quiescence. It publishes only an atomic, coalesced wake hint,
 * not a message queue, second completion, managed record pointer or lease.
 * The configured wake must merely signal source progress, never re-enter
 * this object or CNetManager; the host stops wake publishers before destroy.
 */
typedef struct cflow_cnet_manager_context {
    void *impl;
} cflow_cnet_manager_context;

typedef void (*cflow_cnet_manager_context_wake_fn)(void *user);

typedef struct cflow_cnet_manager_context_config {
    /** Borrowed manager, initialized on this same SOURCE owner. */
    cnet_manager *manager;
    /** BOUND manager record with an existing explicit context hold. */
    cnet_managed_connection managed;
    /** Borrowed route pointers; all source-bound on this owner and tied to
     * exactly the managed connection generation. Config array is copied. */
    cflow_cnet_domain_route *const *routes;
    size_t route_count;
    /** Optional borrowed host wake surface, safe to call from target Owner.
     * Signal only, no poll/retry/release or nested manager call. */
    cflow_cnet_manager_context_wake_fn wake;
    void *wake_user;
} cflow_cnet_manager_context_config;

typedef struct cflow_cnet_manager_context_stats {
    size_t route_count;
    uint64_t notifications;
    uint64_t coalesced_wakes;
    uint64_t release_checks;
    bool notification_pending;
    bool released;
} cflow_cnet_manager_context_stats;

/** Source-owner-only init; validates manager hold and matching exact route
 * connection/owner identities. No implicit reserve/connect/adopt occurs.
 * No partial object is published after failure. */
int cflow_cnet_manager_context_init(
    cflow_cnet_manager_context *context,
    const cflow_cnet_manager_context_config *config);

/** Target Owner: post-ACK coalesced notification. Published hints do not
 * reclaim manager or payload storage. Host must stop publishers before
 * destroying context. Safe to invoke from an Actor action callback. */
int cflow_cnet_manager_context_notify_settled(
    cflow_cnet_manager_context *context);

/** Source Owner: one nonblocking, bounded scan of the fixed associated routes.
 * Returns EBUSY if manager is not RETIRED or ANY route has outstanding CNet
 * receive credit, staged Actor admission or unacknowledged payload. Only a
 * successful source-owned cnet_manager_release_context commits released=true.
 * EALREADY means the single context hold was already released by this guard.
 * out_released is reset to false for all non-success outcomes.
 */
int cflow_cnet_manager_context_poll_release(
    cflow_cnet_manager_context *context,
    bool *out_released);

/** Source Owner: stats snapshot (notification counters are atomic). */
int cflow_cnet_manager_context_get_stats(
    cflow_cnet_manager_context *context,
    cflow_cnet_manager_context_stats *out);

/** Source Owner: destroy only after guard release and ALL target ACK/wake
 * publishers have stopped/joined. The manager itself remains host-owned. */
int cflow_cnet_manager_context_destroy(
    cflow_cnet_manager_context *context);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_CNET_MANAGER_CONTEXT_H */
