#ifndef CFLOW_STATECHART_RUNTIME_H
#define CFLOW_STATECHART_RUNTIME_H

#include <cflow/clock.h>
#include <cflow/executor.h>
#include <cflow/statechart.h>
#include <cflow/timer_event.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cflow_statechart_action_phase {
    CFLOW_STATECHART_ACTION_EXIT = 0,
    CFLOW_STATECHART_ACTION_TRANSITION,
    CFLOW_STATECHART_ACTION_ENTRY,
    CFLOW_STATECHART_ACTION_INITIAL,
    CFLOW_STATECHART_ACTION_HISTORY
} cflow_statechart_action_phase;

/**
 * Copy one internal Event into the current microstep's bounded staging FIFO.
 * `event`, its payload, `user`, and `out_error` are borrowed for this call.
 * Success means the payload was copied; failure returns false and writes a
 * borrowed diagnostic to `*out_error`. The function and `user` are valid only
 * during their enclosing executable callback and must not be retained.
 */
typedef bool (*cflow_statechart_raise_fn)(void *user,
    const cflow_event_view *event, const char **out_error);
/**
 * Evaluate one guard against borrowed immutable state and an optional Event.
 * `event` is non-NULL only for an EVENT trigger; all inputs and payload bytes
 * are valid only until return and must not be retained or modified. On true,
 * the callback must write `*out_enabled` and leave `*out_error` NULL. On
 * false, enabled is ignored; a MAY_FAIL declaration may place a borrowed
 * diagnostic in `*out_error`, which need remain valid only until return.
 */
typedef bool (*cflow_statechart_guard_fn)(void *user, const void *state,
    const cflow_event_view *event, bool *out_enabled,
    const char **out_error);
/**
 * Execute one action over a staged state value.
 *
 * `state` is a borrowed immutable input of the declared state type. `event`
 * and its payload are borrowed until return and are NULL for eventless and
 * completion work. `out_state` is a distinct, non-aliasing writable object of
 * exactly that type; a successful callback must fully initialize it. The
 * next action observes that value. A false return discards `out_state` and all
 * staged raises. Error text follows the guard lifetime rule. `raise_internal`
 * copies a valid Event before returning and may be called repeatedly until it
 * reports bounded-queue/type failure; neither it nor `raise_user` may escape
 * this callback.
 */
typedef bool (*cflow_statechart_executable_fn)(void *user,
    cflow_statechart_action_phase phase, cflow_machine_state_id owner,
    const void *state, const cflow_event_view *event, void *out_state,
    cflow_statechart_raise_fn raise_internal, void *raise_user,
    const char **out_error);

typedef struct cflow_statechart_guard_binding {
    cflow_statechart_guard_id id;
    cflow_statechart_guard_fn fn;
    void *user;
} cflow_statechart_guard_binding;

typedef struct cflow_statechart_executable_binding {
    cflow_statechart_executable_id id;
    cflow_statechart_executable_fn fn;
    void *user;
} cflow_statechart_executable_binding;

typedef enum cflow_statechart_runtime_status {
    CFLOW_STATECHART_RUNTIME_OK = 0,
    CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT,
    CFLOW_STATECHART_RUNTIME_INVALID_EXECUTOR,
    CFLOW_STATECHART_RUNTIME_BINDING_MISMATCH,
    CFLOW_STATECHART_RUNTIME_UNSUPPORTED_TYPE,
    CFLOW_STATECHART_RUNTIME_LIMIT_EXCEEDED,
    CFLOW_STATECHART_RUNTIME_ALLOCATION_FAILED,
    CFLOW_STATECHART_RUNTIME_INVALID_CONFIGURATION,
    CFLOW_STATECHART_RUNTIME_GUARD_FAILED,
    CFLOW_STATECHART_RUNTIME_ACTION_FAILED,
    CFLOW_STATECHART_RUNTIME_INTERNAL_QUEUE_FULL,
    CFLOW_STATECHART_RUNTIME_COMPLETION_QUEUE_FULL,
    CFLOW_STATECHART_RUNTIME_INTERNAL_EVENT_INVALID,
    CFLOW_STATECHART_RUNTIME_INTERNAL_EVENT_TYPE_MISMATCH,
    CFLOW_STATECHART_RUNTIME_MICROSTEP_LIMIT_EXCEEDED,
    CFLOW_STATECHART_RUNTIME_EXECUTOR_FULL,
    CFLOW_STATECHART_RUNTIME_EXECUTOR_CLOSED,
    CFLOW_STATECHART_RUNTIME_TASK_CANCELLED,
    CFLOW_STATECHART_RUNTIME_WOULD_BLOCK
} cflow_statechart_runtime_status;

typedef enum cflow_statechart_snapshot_status {
    CFLOW_STATECHART_SNAPSHOT_OK = 0,
    CFLOW_STATECHART_SNAPSHOT_INVALID_ARGUMENT,
    CFLOW_STATECHART_SNAPSHOT_TOO_SMALL
} cflow_statechart_snapshot_status;

typedef enum cflow_statechart_configuration_status {
    CFLOW_STATECHART_CONFIGURATION_OK = 0,
    CFLOW_STATECHART_CONFIGURATION_INVALID_ARGUMENT,
    CFLOW_STATECHART_CONFIGURATION_UNKNOWN_STATE,
    CFLOW_STATECHART_CONFIGURATION_PSEUDO_STATE,
    CFLOW_STATECHART_CONFIGURATION_DUPLICATE_STATE,
    CFLOW_STATECHART_CONFIGURATION_INVALID_ORDER,
    CFLOW_STATECHART_CONFIGURATION_MISSING_ANCESTOR,
    CFLOW_STATECHART_CONFIGURATION_WRONG_CHILD,
    CFLOW_STATECHART_CONFIGURATION_MISSING_LEAF
} cflow_statechart_configuration_status;

typedef struct cflow_statechart_instance_config {
    /** Borrowed immutable definition; it must outlive instance destruction. */
    const cflow_statechart *statechart;
    /** Copied during initialization using the Statechart state descriptor. */
    const void *initial_state;
    /** Binding rows are copied; each callback `user` pointer remains borrowed. */
    const cflow_statechart_guard_binding *guards;
    size_t guard_count;
    const cflow_statechart_executable_binding *executables;
    size_t executable_count;
    /** Required positive hard bounds; queues never grow beyond these values. */
    size_t external_event_capacity;
    size_t internal_event_capacity;
    size_t completion_capacity;
    size_t microstep_limit;
    /**
     * Zero selects the 64 MiB compile-time instance storage ceiling. The
     * bound covers runtime state, bounded queues, and optional Timer Event
     * slots plus their copied payload storage.
     */
    size_t max_storage_bytes;
    /**
     * Borrowed non-manual SerialExecutor. It and all callback users must
     * outlive instance destruction. Every semantic quantum is posted to it.
     */
    cflow_executor *executor;
    /**
     * Optional borrowed monotonic Clock and fixed Timer Event capacity.
     * Both zero/NULL disable timers; otherwise both must be provided and the
     * Clock must outlive instance destruction. Mutating a shared Clock while
     * timer APIs read it requires synchronization supplied by the caller.
     */
    cflow_clock *clock;
    size_t timer_capacity;
} cflow_statechart_instance_config;

typedef struct cflow_statechart_instance_stats {
    uint64_t configuration_version;
    uint64_t external_accepted;
    uint64_t external_completed;
    uint64_t external_failed;
    uint64_t external_cancelled;
    size_t external_pending;
    size_t external_in_flight;
    uint64_t macrosteps;
    uint64_t microsteps;
    uint64_t actions;
    size_t internal_pending;
    size_t completion_pending;
    size_t active_state_count;
    size_t active_leaf_count;
    cflow_statechart_runtime_status last_status;
    bool closed;
    bool cancelled;
    bool done;
    bool errored;
    /** Zeroed when timers are disabled for this instance. */
    cflow_timer_event_stats timers;
} cflow_statechart_instance_stats;

typedef struct cflow_statechart_instance { void *impl; }
    cflow_statechart_instance;

/**
 * Initialize and stabilize one owning Statechart runtime handle.
 *
 * The initial extended state, binding rows, and all accepted Event payloads
 * are copied. Phase 1 admits only trivial CMeta state/Event storage; managed
 * copy/move/destroy traits are rejected with `UNSUPPORTED_TYPE`. All three
 * queue capacities and `microstep_limit` must be positive. Timer storage, when
 * enabled, is included in `max_storage_bytes`. Initialization returns only
 * after this instance's posted eventless/completion work reaches quiescence;
 * unrelated work on a shared Executor is not awaited. Failure leaves
 * `instance` empty.
 *
 * Guard and executable callbacks run only on the borrowed SerialExecutor.
 * Initial stabilization callbacks may run before this function returns; the
 * instance handle is not yet published, so no public instance API (including
 * close/cancel) is available from those callbacks. After successful return,
 * callbacks may call close/cancel, but must not destroy the instance or wait
 * on the same Executor. External callers may invoke `try_send` concurrently
 * (MPSC); semantic mutation has exactly one Executor owner.
 */
cflow_statechart_runtime_status cflow_statechart_instance_init(
    cflow_statechart_instance *instance,
    const cflow_statechart_instance_config *config);

/**
 * Copy one typed external Event into the bounded mailbox without blocking.
 *
 * `OK` means owned mailbox admission, not successful execution. `FULL`,
 * `CLOSED`, `CANCELLED`, type mismatch, and invalid Event/schema admission are
 * returned unchanged from the mailbox boundary. If a later Executor post is
 * rejected, this call still returns `OK`; the first precise `EXECUTOR_FULL` or
 * `EXECUTOR_CLOSED` failure and exactly-once settlement are observable through
 * `get_stats` and `error`.
 */
cflow_mailbox_status cflow_statechart_instance_try_send(
    cflow_statechart_instance *instance, const cflow_event_view *event);

/**
 * Schedule one copied Event at an absolute deadline in an active real-state
 * scope. Unknown, pseudo, or inactive scopes return `INVALID_ARGUMENT`.
 * Equal deadlines fire in schedule order. The Event payload is copied before
 * this call returns and remains owned by the instance until firing or cancel.
 */
cflow_timer_event_schedule_result cflow_statechart_instance_try_schedule_at(
    cflow_statechart_instance *instance,
    cflow_machine_state_id scope,
    cflow_deadline deadline,
    const cflow_event_view *event);
/**
 * Schedule one copied Event after a duration in an active real-state scope.
 * Scope, payload ownership, and equal-deadline ordering match `schedule_at`.
 */
cflow_timer_event_schedule_result cflow_statechart_instance_try_schedule_after(
    cflow_statechart_instance *instance,
    cflow_machine_state_id scope,
    cflow_duration delay,
    const cflow_event_view *event);
/**
 * Cancel one pending scoped timer. `FIRE_WON` means delivery already claimed
 * the timer and will attempt exactly one external Mailbox admission.
 */
cflow_timer_event_status cflow_statechart_instance_cancel_timer(
    cflow_statechart_instance *instance,
    cflow_timer_event_id timer_id);
/**
 * Hand off at most one ready timer Event to this instance's external Mailbox.
 * Mailbox FULL/CLOSED/CANCELLED is returned in the fire result and is never
 * retried. Delivery enters the normal run-to-completion path.
 */
cflow_timer_event_fire_result cflow_statechart_instance_run_one_ready_timer(
    cflow_statechart_instance *instance);

/**
 * Stop admission, cancel all pending timers, preserve a microstep whose commit
 * wins, wait for any already claimed timer handoff, then reach DONE. No-op
 * after any clean, controlled, or error terminal outcome already won.
 */
void cflow_statechart_instance_close(cflow_statechart_instance *instance);
/**
 * Stop admission, cancel all pending timers, and discard a microstep if
 * cancellation wins before commit. Any already claimed timer handoff settles
 * before return. No-op after any terminal outcome already won.
 */
void cflow_statechart_instance_cancel(cflow_statechart_instance *instance);

/**
 * Copy the document-ordered active real-state IDs and publication version.
 * A short buffer returns `TOO_SMALL`, reports the required count, and writes
 * no partial state list. The output remains caller-owned.
 */
cflow_statechart_snapshot_status cflow_statechart_instance_copy_configuration(
    const cflow_statechart_instance *instance,
    cflow_machine_state_id *out_states, size_t state_capacity,
    size_t *out_state_count, uint64_t *out_version);
cflow_machine_state_id cflow_statechart_instance_current_state(
    const cflow_statechart_instance *instance);
/** Copy the published extended state and return its borrowed type descriptor. */
bool cflow_statechart_instance_copy_state(
    const cflow_statechart_instance *instance,
    const cmeta_type_desc **out_type, void *out_state,
    size_t state_capacity);
/**
 * Copy one linearizable accounting snapshot. At every snapshot, including
 * while producers and the driver are active:
 * accepted = sat(completed + failed + cancelled + pending + in_flight),
 * where every counter and intermediate sum saturates at `UINT64_MAX`.
 */
bool cflow_statechart_instance_get_stats(
    const cflow_statechart_instance *instance,
    cflow_statechart_instance_stats *out);
/** Borrowed stable first-error text, or NULL; invalidated by destroy. */
const char *cflow_statechart_instance_error(
    const cflow_statechart_instance *instance);
/**
 * Wait for this instance's posted work to settle, free owned storage, and
 * clear the handle. Unrelated work on a shared Executor is not awaited.
 * Returns `WOULD_BLOCK` from a callback on the same Executor without freeing.
 * Concurrent admission/control calls must already have stopped.
 */
cflow_statechart_runtime_status cflow_statechart_instance_destroy(
    cflow_statechart_instance *instance);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_STATECHART_RUNTIME_H */
