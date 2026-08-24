#ifndef CFLOW_MACHINE_RUNTIME_H
#define CFLOW_MACHINE_RUNTIME_H

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

typedef enum cflow_machine_runtime_status {
    CFLOW_MACHINE_RUNTIME_OK = 0,
    CFLOW_MACHINE_RUNTIME_INVALID_ARGUMENT,
    CFLOW_MACHINE_RUNTIME_INVALID_EXECUTOR,
    CFLOW_MACHINE_RUNTIME_BINDING_MISMATCH,
    CFLOW_MACHINE_RUNTIME_UNSUPPORTED_TYPE,
    CFLOW_MACHINE_RUNTIME_TYPE_MISMATCH,
    CFLOW_MACHINE_RUNTIME_ALLOCATION_FAILED
} cflow_machine_runtime_status;

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
cflow_machine_runtime_status cflow_machine_instance_init(
    cflow_machine_instance *instance,
    const cflow_machine_instance_config *config);

/** Copy one mutex-consistent state snapshot into caller-owned storage. */
bool cflow_machine_instance_copy_state(
    const cflow_machine_instance *instance,
    const cmeta_type_desc **out_type,
    void *out_value,
    size_t out_value_capacity);

/** Return the current state ID, or zero for an empty handle. */
cflow_machine_state_id cflow_machine_instance_current_state(
    const cflow_machine_instance *instance);

/** Read one mutex-consistent runtime and Mailbox statistics snapshot. */
bool cflow_machine_instance_get_stats(
    const cflow_machine_instance *instance,
    cflow_machine_instance_stats *out);

/** Return the first owned runtime error, or NULL when none has occurred. */
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

#endif /* CFLOW_MACHINE_RUNTIME_H */
