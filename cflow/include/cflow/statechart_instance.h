#ifndef CFLOW_STATECHART_INSTANCE_H
#define CFLOW_STATECHART_INSTANCE_H

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
 * completion work. `out_state` is distinct, non-aliasing, uninitialized
 * storage of exactly that type; a successful callback must fully construct it.
 * A false callback must leave it uninitialized. The next action observes a
 * successful value, and the instance destroys it exactly once when it is
 * replaced or rolled back. A false return also discards all staged raises.
 * Error text follows the guard lifetime rule. `raise_internal`
 * copies a valid Event before returning and may be called repeatedly until it
 * reports bounded-queue/type failure; neither it nor `raise_user` may escape
 * this callback.
 */
typedef bool (*cflow_statechart_executable_fn)(void *user,
    cflow_statechart_action_phase phase, cflow_machine_state_id owner,
    const void *state, const cflow_event_view *event, void *out_state,
    cflow_statechart_raise_fn raise_internal, void *raise_user,
    const char **out_error);

/**
 * Query the action-time active configuration for one declared real state.
 * The callback and `user` are borrowed only for the enclosing contextual
 * executable call and must not be retained. Unknown and pseudo-state IDs are
 * not active and return false.
 */
typedef bool (*cflow_statechart_is_active_fn)(void *user,
    cflow_machine_state_id state);

/** Borrowed arguments for one contextual guard callback. */
typedef struct cflow_statechart_guard_context {
    const void *state;
    /** Non-NULL only while selecting an Event-triggered transition. */
    const cflow_event_view *event;
    /** Valid only until the contextual guard returns. */
    cflow_statechart_is_active_fn is_active;
    void *configuration_user;
} cflow_statechart_guard_context;

/**
 * Copy one Event into the current transition-selection transaction.
 *
 * The Event is committed after successful selection even when every guard is
 * disabled and no transition is selected. Selection-raised Events preserve
 * guard evaluation order and precede Events raised by the selected
 * microstep's exit, transition, and entry actions. A failed copy makes the
 * enclosing guard fail even if its callback ignores the returned false.
 * `context`, `event`, its payload, and `out_error` are call-scoped borrows.
 */
bool cflow_statechart_guard_context_raise_internal(
    const cflow_statechart_guard_context *context,
    const cflow_event_view *event, uint64_t origin_token,
    const char **out_error);

/**
 * Evaluate one guard with a call-scoped active-configuration query.
 * State, Event, enabled, error, and no-retention rules are identical to
 * `cflow_statechart_guard_fn`. The context and every borrowed member are
 * invalid after return and must not be retained.
 */
typedef bool (*cflow_statechart_contextual_guard_fn)(
    void *user, const cflow_statechart_guard_context *context,
    bool *out_enabled, const char **out_error);

/**
 * One move-only external-effect reservation prepared by an action callback.
 *
 * Successful staging transfers the ticket to the Statechart instance. The
 * instance calls `commit(user)` exactly once after the owning microstep is
 * published, or `discard(user)` exactly once when that microstep rolls back.
 * Both callbacks must be nonblocking, infallible, and must not destroy or wait
 * on the owning instance. Failed staging leaves ownership with the caller.
 */
typedef struct cflow_statechart_effect_ticket {
    void (*commit)(void *user);
    void (*discard)(void *user);
    void *user;
} cflow_statechart_effect_ticket;

typedef bool (*cflow_statechart_stage_effect_fn)(
    void *user, const cflow_statechart_effect_ticket *ticket,
    const char **out_error);

/** Borrowed arguments for one contextual executable callback. */
typedef struct cflow_statechart_executable_context {
    cflow_statechart_action_phase phase;
    cflow_machine_state_id owner;
    const void *state;
    const cflow_event_view *event;
    void *out_state;
    cflow_statechart_raise_fn raise_internal;
    void *raise_user;
    /** Optional bounded transactional external-effect staging. */
    cflow_statechart_stage_effect_fn stage_effect;
    void *effect_user;
    /** Valid only until the contextual executable returns. */
    cflow_statechart_is_active_fn is_active;
    void *configuration_user;
    /**
     * Stage one internal Event with an opaque source tag. The Event and tag
     * become visible together only if the owning microstep commits. The
     * callback and all arguments are call-scoped.
     */
    bool (*raise_internal_tagged)(
        void *user, const cflow_event_view *event, uint64_t origin_token,
        const char **out_error);
} cflow_statechart_executable_context;

/**
 * Execute one action with a call-scoped active-configuration query.
 * State, Event, output, raise, error, and non-aliasing rules are identical to
 * `cflow_statechart_executable_fn`. The context and every borrowed member are
 * invalid after return and must not be retained.
 */
typedef bool (*cflow_statechart_contextual_executable_fn)(
    void *user, const cflow_statechart_executable_context *context,
    const char **out_error);

#define CFLOW_STATECHART_INSTANCE_HOOKS_ABI_V4 4u

typedef enum cflow_statechart_observed_event_kind {
    CFLOW_STATECHART_OBSERVED_EXTERNAL = 1,
    CFLOW_STATECHART_OBSERVED_INTERNAL,
    CFLOW_STATECHART_OBSERVED_COMPLETION
} cflow_statechart_observed_event_kind;

/** Call-scoped event observation delivered immediately before selection. */
typedef struct cflow_statechart_observed_event {
    cflow_statechart_observed_event_kind kind;
    /** Non-NULL for external/internal Events. */
    const cflow_event_view *event;
    /** Nonzero only for a completion observation. */
    cflow_machine_state_id completion;
    /** Origin tag admitted with an external or tagged internal Event. */
    uint64_t origin_token;
} cflow_statechart_observed_event;

typedef enum cflow_statechart_host_phase {
    CFLOW_STATECHART_HOST_PREPARE_TRIGGER = 1,
    CFLOW_STATECHART_HOST_PREPARE_QUIESCENCE
} cflow_statechart_host_phase;

typedef enum cflow_statechart_host_result {
    CFLOW_STATECHART_HOST_CONTINUE = 0,
    CFLOW_STATECHART_HOST_DROP,
    CFLOW_STATECHART_HOST_FATAL
} cflow_statechart_host_result;

/** Opaque call-scoped transaction context owned by the Statechart runtime. */
typedef struct cflow_statechart_host_context
    cflow_statechart_host_context;

/** Return the current host transaction phase. */
cflow_statechart_host_phase cflow_statechart_host_context_phase(
    const cflow_statechart_host_context *context);

/**
 * Return the call-scoped trigger, or NULL during PREPARE_QUIESCENCE.
 * Event payload and metadata remain borrowed and must not be retained.
 */
const cflow_statechart_observed_event *cflow_statechart_host_context_trigger(
    const cflow_statechart_host_context *context);

/** Return the immutable currently published managed state. */
const void *cflow_statechart_host_context_state(
    const cflow_statechart_host_context *context);

/** Return the current active-configuration version. */
uint64_t cflow_statechart_host_context_configuration_version(
    const cflow_statechart_host_context *context);

/** Query call-scoped membership in the current active configuration. */
bool cflow_statechart_host_context_is_active(
    const cflow_statechart_host_context *context,
    cflow_machine_state_id state);

/**
 * Lazily copy-construct and return the sole writable staged state.
 * Repeated calls return the same pointer. On failure, NULL is returned and
 * `out_error` receives a call-scoped deterministic diagnostic.
 */
void *cflow_statechart_host_context_edit_state(
    cflow_statechart_host_context *context, const char **out_error);

/** Copy one Event into the transaction's bounded internal FIFO journal. */
bool cflow_statechart_host_context_raise_internal(
    cflow_statechart_host_context *context, const cflow_event_view *event,
    uint64_t origin_token, const char **out_error);

/** Move one effect ticket into the transaction's bounded effect journal. */
bool cflow_statechart_host_context_stage_effect(
    cflow_statechart_host_context *context,
    const cflow_statechart_effect_ticket *ticket, const char **out_error);

/**
 * Prepare one format-neutral host transaction on the SerialExecutor.
 * The context and every borrowed value are invalid after return.
 */
typedef cflow_statechart_host_result (*cflow_statechart_host_transaction_fn)(
    void *user, cflow_statechart_host_context *context,
    const char **out_error);

/**
 * Optional format-neutral V4 instance boundary copied during initialization.
 * Callbacks run on the SerialExecutor without the instance mutex held. They
 * must not retain any context member, wait on, or destroy the instance.
 */
typedef struct cflow_statechart_instance_hooks {
    uint32_t abi_version;
    size_t struct_size;
    /** Sole callback for trigger preparation and quiescence preparation. */
    cflow_statechart_host_transaction_fn on_host_transaction;
} cflow_statechart_instance_hooks;

typedef struct cflow_statechart_guard_binding {
    cflow_statechart_guard_id id;
    cflow_statechart_guard_fn fn;
    void *user;
    /** Exactly one of `fn` and `contextual_fn` must be non-NULL. */
    cflow_statechart_contextual_guard_fn contextual_fn;
} cflow_statechart_guard_binding;

typedef struct cflow_statechart_executable_binding {
    cflow_statechart_executable_id id;
    cflow_statechart_executable_fn fn;
    void *user;
    /** Exactly one of `fn` and `contextual_fn` must be non-NULL. */
    cflow_statechart_contextual_executable_fn contextual_fn;
} cflow_statechart_executable_binding;

typedef enum cflow_statechart_instance_status {
    CFLOW_STATECHART_INSTANCE_OK = 0,
    CFLOW_STATECHART_INSTANCE_INVALID_ARGUMENT,
    CFLOW_STATECHART_INSTANCE_INVALID_EXECUTOR,
    CFLOW_STATECHART_INSTANCE_BINDING_MISMATCH,
    CFLOW_STATECHART_INSTANCE_UNSUPPORTED_TYPE,
    CFLOW_STATECHART_INSTANCE_LIMIT_EXCEEDED,
    CFLOW_STATECHART_INSTANCE_ALLOCATION_FAILED,
    CFLOW_STATECHART_INSTANCE_INVALID_CONFIGURATION,
    CFLOW_STATECHART_INSTANCE_GUARD_FAILED,
    CFLOW_STATECHART_INSTANCE_ACTION_FAILED,
    CFLOW_STATECHART_INSTANCE_INTERNAL_QUEUE_FULL,
    CFLOW_STATECHART_INSTANCE_COMPLETION_QUEUE_FULL,
    CFLOW_STATECHART_INSTANCE_INTERNAL_EVENT_INVALID,
    CFLOW_STATECHART_INSTANCE_INTERNAL_EVENT_TYPE_MISMATCH,
    CFLOW_STATECHART_INSTANCE_EFFECT_JOURNAL_FULL,
    CFLOW_STATECHART_INSTANCE_MICROSTEP_LIMIT_EXCEEDED,
    CFLOW_STATECHART_INSTANCE_EXECUTOR_FULL,
    CFLOW_STATECHART_INSTANCE_EXECUTOR_CLOSED,
    CFLOW_STATECHART_INSTANCE_TASK_CANCELLED,
    CFLOW_STATECHART_INSTANCE_WOULD_BLOCK,
    CFLOW_STATECHART_INSTANCE_HOOK_FAILED
} cflow_statechart_instance_status;

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
     * bound covers instance state, bounded queues, optional Timer Event slots
     * plus copied payload storage, optional effect-ticket storage, and the
     * optional adapter-internal copied-Event mailbox.
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
    /**
     * Optional fixed count of move-only effect tickets staged by contextual
     * actions. Zero disables effect staging. Ticket storage is included in
     * `max_storage_bytes` and never grows after initialization.
     */
    size_t effect_capacity;
    /**
     * Optional MPSC copied-Event ingress for asynchronous adapter results.
     * Zero disables it. Accepted Events are consumed by the SerialExecutor
     * after already staged internal Events and before external mailbox Events.
     */
    size_t adapter_internal_event_capacity;
    /**
     * Optional copied versioned hook table. `hook_user` remains borrowed
     * until instance destruction and is passed to every configured callback.
     */
    const cflow_statechart_instance_hooks *hooks;
    void *hook_user;
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
    uint64_t adapter_internal_accepted;
    size_t adapter_internal_pending;
    size_t active_state_count;
    size_t active_leaf_count;
    cflow_statechart_instance_status last_status;
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
 * Initialize and stabilize one owning Statechart instance handle.
 *
 * The initial extended state, binding rows, and all accepted Event payloads
 * are copied. Extended state may either be trivial or provide CMeta
 * copy/move/destroy traits. Event payloads remain trivial storage; managed
 * Event traits are rejected with `UNSUPPORTED_TYPE`. All three queue capacities
 * and `microstep_limit` must be positive. Timer storage, when
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
cflow_statechart_instance_status cflow_statechart_instance_init(
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
 * Copy one external Event together with an opaque nonzero source identity.
 * The token is FIFO-aligned with the Event and is visible only to the optional
 * external preprocess hook. Token zero has the same semantics as `try_send`.
 */
cflow_mailbox_status cflow_statechart_instance_try_send_tagged(
    cflow_statechart_instance *instance, const cflow_event_view *event,
    uint64_t origin_token);

/**
 * Copy one typed adapter result into the bounded internal-priority ingress.
 *
 * Successful MPSC admission transfers an independent trivial payload copy.
 * The SerialExecutor consumes normal staged internal Events first, then this
 * ingress, and only then an external Event. Disabled ingress, unknown Events,
 * type mismatch, full, close, and cancellation are returned explicitly.
 */
cflow_mailbox_status cflow_statechart_instance_try_send_internal(
    cflow_statechart_instance *instance, const cflow_event_view *event);

/**
 * Attach the instance's terminal lifecycle as one borrowed Resumable.
 *
 * The adapter returns WAIT while the Statechart is active, DONE after clean
 * completion/close/cancel settles, and ERROR for the stable first error. It
 * never returns VALUE or VALUE_AND_DONE; `output_type` is the Statechart state
 * type as an empty-sequence type witness. Only one Resumable or Publisher adapter
 * may be attached at a time. Destroying the adapter cancels and detaches it,
 * but does not destroy the instance. `out` must be zero-initialized; rejection
 * leaves both the instance and destination unchanged.
 */
bool cflow_statechart_instance_as_terminal_resumable(
    cflow_statechart_instance *instance, cflow_resumable *out);

/** Attach the same terminal-only projection to a zero-initialized Publisher. */
bool cflow_statechart_instance_as_terminal_publisher(
    cflow_statechart_instance *instance, cflow_publisher *out);

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
 * Stop admission and asynchronously execute every active state's exit actions
 * on the owning SerialExecutor before reaching the cancelled terminal state.
 * A microstep that has already reached commit remains visible. Repeated calls
 * and calls after a terminal winner are no-ops.
 */
void cflow_statechart_instance_request_exit(
    cflow_statechart_instance *instance);
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
/**
 * Byte-copy the published trivial extended state and return its borrowed type
 * descriptor. Managed state returns false without writing output or exposing a
 * descriptor; use executor-owned actions to observe it.
 */
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
 * An attached terminal adapter also returns `WOULD_BLOCK`; destroy that
 * adapter first. The instance handle remains valid in either case.
 * Concurrent admission/control calls must already have stopped.
 */
cflow_statechart_instance_status cflow_statechart_instance_destroy(
    cflow_statechart_instance *instance);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_STATECHART_INSTANCE_H */
