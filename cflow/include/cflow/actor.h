#ifndef CFLOW_ACTOR_H
#define CFLOW_ACTOR_H

#include <cflow/machine_instance.h>
#include <cflow/statechart_instance.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cflow_actor_state {
    CFLOW_ACTOR_STATE_START = 0,
    CFLOW_ACTOR_STATE_RUNNING,
    CFLOW_ACTOR_STATE_STOPPING,
    CFLOW_ACTOR_STATE_STOPPED,
    CFLOW_ACTOR_STATE_FAILED
} cflow_actor_state;

typedef enum cflow_actor_status {
    CFLOW_ACTOR_OK = 0,
    CFLOW_ACTOR_INVALID_ARGUMENT,
    CFLOW_ACTOR_INVALID_SCHEDULER,
    CFLOW_ACTOR_MACHINE_REJECTED,
    CFLOW_ACTOR_ALLOCATION_FAILED,
    CFLOW_ACTOR_ALREADY_STARTED,
    CFLOW_ACTOR_STOPPING,
    CFLOW_ACTOR_STOPPED,
    CFLOW_ACTOR_FAILED,
    /** Appended to preserve every existing Actor status value. */
    CFLOW_ACTOR_STATECHART_REJECTED
} cflow_actor_status;

typedef enum cflow_actor_send_status {
    CFLOW_ACTOR_SEND_ACCEPTED = 0,
    CFLOW_ACTOR_SEND_INVALID_ARGUMENT,
    CFLOW_ACTOR_SEND_TYPE_MISMATCH,
    CFLOW_ACTOR_SEND_FULL,
    CFLOW_ACTOR_SEND_NOT_STARTED,
    CFLOW_ACTOR_SEND_STOPPING,
    CFLOW_ACTOR_SEND_STOPPED,
    CFLOW_ACTOR_SEND_FAILED,
    CFLOW_ACTOR_SEND_STALE
} cflow_actor_send_status;

/**
 * Actor configuration copied during initialization.
 *
 * The Actor owns the resulting Machine instance, its fixed-capacity Mailbox,
 * identity Graph, Subscription, and lifecycle control block. It borrows the immutable
 * Machine declaration, SerialExecutor, concurrent Scheduler, type descriptors,
 * guard/action functions and user data, and sink callback functions/user data.
 * All borrowed objects must remain valid until `cflow_actor_destroy` returns;
 * the Executor and Scheduler must remain operational through that call.
 * Subscriber values are borrowed only for the callback duration. Subscriber
 * callbacks run
 * without the Actor gate held, so self-send and `cflow_actor_request_stop` are
 * permitted; `cflow_actor_wait` and destruction from callbacks are forbidden.
 */
typedef struct cflow_actor_config {
    cflow_machine_instance_config machine;
    cflow_scheduler *scheduler;
    cflow_subscriber_callbacks callbacks;
} cflow_actor_config;

typedef struct cflow_actor_init_result {
    cflow_actor_status status;
    cflow_machine_instance_status machine_status;
} cflow_actor_init_result;

typedef struct cflow_actor_stats {
    cflow_actor_state state;
    cflow_machine_instance_stats machine;
    uint64_t rejected_not_started;
    uint64_t rejected_stopping;
    uint64_t rejected_stopped;
    uint64_t rejected_failed;
    uint64_t rejected_stale;
} cflow_actor_stats;

/**
 * Statechart-backed Actor configuration copied during initialization.
 *
 * The Actor owns the resulting Statechart instance, its terminal-only
 * Publisher, identity Graph, Subscription, and lifecycle shell. The
 * Statechart declaration,
 * SerialExecutor, optional Clock, concurrent Scheduler, bindings, and callback
 * contexts are borrowed until `cflow_actor_destroy` returns. The terminal
 * Publisher emits no values: root FINAL or explicit close calls `on_done`,
 * while the stable first instance error calls `on_error`.
 */
typedef struct cflow_statechart_actor_config {
    cflow_statechart_instance_config statechart;
    cflow_scheduler *scheduler;
    cflow_subscriber_callbacks callbacks;
} cflow_statechart_actor_config;

typedef struct cflow_statechart_actor_init_result {
    cflow_actor_status status;
    cflow_statechart_instance_status statechart_status;
} cflow_statechart_actor_init_result;

typedef struct cflow_statechart_actor_stats {
    cflow_actor_state state;
    cflow_statechart_instance_stats statechart;
    uint64_t rejected_not_started;
    uint64_t rejected_stopping;
    uint64_t rejected_stopped;
    uint64_t rejected_failed;
    uint64_t rejected_stale;
} cflow_statechart_actor_stats;

/** Owner handle retaining one root reference. Zero initialization is required. */
typedef struct cflow_actor {
    void *impl;
} cflow_actor;

/** Independently retained producer handle. Zero initialization is required. */
typedef struct cflow_actor_ref {
    void *impl;
} cflow_actor_ref;

/**
 * Initialize an Actor in `START` without starting its Subscription.
 *
 * `machine_status` is `CFLOW_MACHINE_INSTANCE_OK` unless the returned status is
 * `CFLOW_ACTOR_MACHINE_REJECTED`, when it preserves the exact Machine rejection.
 * The scheduler must be valid and advertise `CMETA_SCHED_CAP_CONCURRENT`.
 */
cflow_actor_init_result cflow_actor_init(
    cflow_actor *actor, const cflow_actor_config *config);

/**
 * Initialize a Statechart-backed Actor in `START`.
 *
 * `statechart_status` is `CFLOW_STATECHART_INSTANCE_OK` unless status is
 * `CFLOW_ACTOR_STATECHART_REJECTED`, when the exact instance rejection is
 * preserved. The scheduler must be concurrent. Initialization may execute
 * Statechart initial stabilization before returning, as specified by the
 * embedded instance configuration.
 */
cflow_statechart_actor_init_result cflow_statechart_actor_init(
    cflow_actor *actor, const cflow_statechart_actor_config *config);

/** Start the single owned Subscription and request `SIZE_MAX` demand. */
cflow_actor_status cflow_actor_start(cflow_actor *actor);

/**
 * Stop admission before closing the owned Machine or Statechart instance. The
 * first request is successful; later calls report the exact current or
 * terminal lifecycle status.
 */
cflow_actor_status cflow_actor_request_stop(cflow_actor *actor);

/**
 * Block until `STOPPED` or `FAILED`.
 *
 * This control-plane call must not run from a Machine/Statechart guard or
 * action, Actor sink callback, scheduler worker callback, or concurrently with
 * destruction.
 */
cflow_actor_state cflow_actor_wait(cflow_actor *actor);

/** Return the current lifecycle state; an empty handle reports `START`. */
cflow_actor_state cflow_actor_current_state(const cflow_actor *actor);

/** Read one mutex-consistent Actor and Machine statistics snapshot. */
bool cflow_actor_get_stats(const cflow_actor *actor, cflow_actor_stats *out);

/**
 * Read one mutex-consistent Actor and Statechart statistics snapshot.
 * Returns false without changing `out` for an invalid or Machine-backed Actor.
 */
bool cflow_statechart_actor_get_stats(
    const cflow_actor *actor, cflow_statechart_actor_stats *out);

/** Return the first Actor-owned failure text, or NULL when no failure exists. */
const char *cflow_actor_error(const cflow_actor *actor);

/** Acquire an independent producer reference from a live owner handle. */
bool cflow_actor_ref_acquire(const cflow_actor *actor, cflow_actor_ref *out);

/** Retain another producer reference from an existing reference. */
bool cflow_actor_ref_retain(const cflow_actor_ref *ref, cflow_actor_ref *out);

/** Release one producer reference and clear its handle. */
void cflow_actor_ref_release(cflow_actor_ref *ref);

/**
 * Try to copy one Event into the Actor-owned bounded Mailbox.
 *
 * The Event view is borrowed only for this call. Accepted payload bytes become
 * an Actor-owned bounded trivial copy. This MPMC-producer admission path never
 * blocks, retries, overwrites, resizes, silently drops, or allocates.
 */
cflow_actor_send_status cflow_actor_ref_try_send(
    const cflow_actor_ref *ref, const cflow_event_view *event);

/**
 * Mark producer refs stale, synchronously close the selected instance and
 * Subscription,
 * clear the owner handle, and release the root reference. The small control
 * block is reclaimed after the last producer ref is released. Destruction
 * must not run from callbacks and requires owner-side serialization.
 */
void cflow_actor_destroy(cflow_actor *actor);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_ACTOR_H */
