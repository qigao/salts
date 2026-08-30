#ifndef CFLOW_MACHINE_INSTANCE_H
#define CFLOW_MACHINE_INSTANCE_H

#include <cflow/executor.h>
#include <cflow/machine.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*cflow_machine_guard_fn)(
    void *user,
    const void *state,
    const void *event,
    bool *out_enabled,
    const char **out_error);

typedef bool (*cflow_machine_action_fn)(
    void *user,
    const void *state,
    const void *event,
    void *out_target_state,
    void *out_observation,
    const char **out_error);

typedef struct cflow_machine_guard_binding {
    cflow_machine_guard_id id;
    cflow_machine_guard_fn fn;
    void *user;
} cflow_machine_guard_binding;

typedef struct cflow_machine_action_binding {
    cflow_machine_action_id id;
    cflow_machine_action_fn fn;
    void *user;
} cflow_machine_action_binding;

typedef enum cflow_machine_instance_status {
    CFLOW_MACHINE_INSTANCE_OK = 0,
    CFLOW_MACHINE_INSTANCE_INVALID_ARGUMENT,
    CFLOW_MACHINE_INSTANCE_INVALID_EXECUTOR,
    CFLOW_MACHINE_INSTANCE_BINDING_MISMATCH,
    CFLOW_MACHINE_INSTANCE_UNSUPPORTED_TYPE,
    CFLOW_MACHINE_INSTANCE_TYPE_MISMATCH,
    CFLOW_MACHINE_INSTANCE_ALLOCATION_FAILED
} cflow_machine_instance_status;

typedef struct cflow_machine_instance_config {
    const cflow_machine *machine;
    const void *initial_state;
    const cmeta_type_desc *output_type;
    const cflow_machine_guard_binding *guards;
    size_t guard_count;
    const cflow_machine_action_binding *actions;
    size_t action_count;
    size_t mailbox_capacity;
    cflow_executor *executor;
} cflow_machine_instance_config;

typedef struct cflow_machine_instance_stats {
    uint64_t accepted;
    uint64_t completed;
    uint64_t failed;
    uint64_t cancelled_events;
    uint64_t emitted_values;
    uint64_t emitted_events;
    size_t pending;
    size_t in_flight;
    cflow_machine_state_id current_state;
    bool closed;
    bool cancelled;
    bool done;
    bool errored;
} cflow_machine_instance_stats;

typedef struct cflow_machine_instance {
    void *impl;
} cflow_machine_instance;

/**
 * Create one Machine execution instance without publishing partial state.
 *
 * The instance copies initial state bytes and binding rows. It borrows the
 * immutable Machine, SerialExecutor, callbacks, and callback user data until
 * destroy. The executor must be a non-manual SerialExecutor.
 */
cflow_machine_instance_status cflow_machine_instance_init(
    cflow_machine_instance *instance,
    const cflow_machine_instance_config *config);

/** Copy one typed Event into the instance-owned bounded Mailbox. */
cflow_mailbox_status cflow_machine_instance_try_send(
    cflow_machine_instance *instance,
    const cflow_event_view *event);

/**
 * Attach one Resumable adapter borrowing this instance.
 *
 * Only one Resumable or Publisher adapter may be attached at a time. The
 * adapter's destroy operation detaches it but does not destroy the instance.
 */
bool cflow_machine_instance_as_resumable(
    cflow_machine_instance *instance,
    cflow_resumable *out);

/** Attach one Publisher adapter borrowing this instance. */
bool cflow_machine_instance_as_publisher(
    cflow_machine_instance *instance,
    cflow_publisher *out);

/**
 * Gracefully stop admission, cancel queued Events, and preserve an in-flight
 * transition commit. If close overlaps an executing transition, that
 * transition commits exactly once before terminal settlement. Repeated calls
 * are idempotent.
 */
void cflow_machine_instance_close(cflow_machine_instance *instance);

/**
 * Stop admission and cancel queued Events.
 *
 * Cancellation and transition commit are linearized by the instance mutex.
 * Cancellation that wins before commit discards the staged state and
 * observation. A commit that wins first remains observable exactly once,
 * including a prepared VALUE, and cancellation prevents subsequent Events.
 * Action callback effects outside Machine-owned state are not rolled back.
 * Repeated calls are idempotent.
 */
void cflow_machine_instance_cancel(cflow_machine_instance *instance);

/** Copy one mutex-consistent state snapshot into caller-owned storage. */
bool cflow_machine_instance_copy_state(
    const cflow_machine_instance *instance,
    const cmeta_type_desc **out_type,
    void *out_value,
    size_t out_value_capacity);

/** Return the current state ID, or zero for an empty handle. */
cflow_machine_state_id cflow_machine_instance_current_state(
    const cflow_machine_instance *instance);

/** Read one mutex-consistent instance and Mailbox statistics snapshot. */
bool cflow_machine_instance_get_stats(
    const cflow_machine_instance *instance,
    cflow_machine_instance_stats *out);

/** Return the first owned instance error, or NULL when none has occurred. */
const char *cflow_machine_instance_error(
    const cflow_machine_instance *instance);

/**
 * Destroy a quiescent instance and clear its handle.
 *
 * Producers and any attached adapter must already be quiescent. The borrowed
 * Machine, executor, callbacks, and user data may be released afterwards.
 */
void cflow_machine_instance_destroy(cflow_machine_instance *instance);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_MACHINE_INSTANCE_H */
