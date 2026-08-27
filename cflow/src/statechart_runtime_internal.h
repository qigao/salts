#ifndef CFLOW_STATECHART_RUNTIME_INTERNAL_H
#define CFLOW_STATECHART_RUNTIME_INTERNAL_H

#include <cflow/executor.h>
#include <cflow/statechart.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef CFLOW_STATECHART_MAX_INSTANCE_BYTES
#define CFLOW_STATECHART_MAX_INSTANCE_BYTES 67108864u
#endif

#ifndef CFLOW_STATECHART_ERROR_CAPACITY
#define CFLOW_STATECHART_ERROR_CAPACITY 512u
#endif

/*
 * @internal @incomplete
 *
 * This runtime surface is private until Task 5 completes actions, queues, and
 * run-to-completion. It may change between intermediate Statechart commits and
 * must not be installed or included by cflow/cflow.h.
 */

typedef enum cflow_statechart_action_phase {
    CFLOW_STATECHART_ACTION_EXIT = 0,
    CFLOW_STATECHART_ACTION_TRANSITION,
    CFLOW_STATECHART_ACTION_ENTRY,
    CFLOW_STATECHART_ACTION_INITIAL,
    CFLOW_STATECHART_ACTION_HISTORY
} cflow_statechart_action_phase;

typedef bool (*cflow_statechart_raise_fn)(
    void *user, const cflow_event_view *event, const char **out_error);

typedef bool (*cflow_statechart_guard_fn)(
    void *user,
    const void *state,
    const cflow_event_view *event,
    bool *out_enabled,
    const char **out_error);

typedef bool (*cflow_statechart_executable_fn)(
    void *user,
    cflow_statechart_action_phase phase,
    cflow_machine_state_id owner,
    const void *state,
    const cflow_event_view *event,
    void *out_state,
    cflow_statechart_raise_fn raise_internal,
    void *raise_user,
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
    const cflow_statechart *statechart;
    const void *initial_state;
    const cflow_statechart_guard_binding *guards;
    size_t guard_count;
    const cflow_statechart_executable_binding *executables;
    size_t executable_count;
    /**
     * Zero selects CFLOW_STATECHART_MAX_INSTANCE_BYTES. A nonzero value is the
     * exact per-instance hard limit and must not exceed that compile ceiling.
     */
    size_t max_storage_bytes;
    cflow_executor *executor;
} cflow_statechart_instance_config;

typedef struct cflow_statechart_storage_requirements {
    size_t control_bytes;
    size_t binding_bytes;
    size_t configuration_bytes;
    size_t history_bitset_bytes;
    size_t history_count_bytes;
    size_t extended_state_bytes;
    size_t index_work_bytes;
    size_t total_bytes;
} cflow_statechart_storage_requirements;

typedef struct cflow_statechart_instance_stats {
    uint64_t configuration_version;
    uint64_t macrosteps;
    uint64_t microsteps;
    uint64_t actions;
    size_t active_state_count;
    size_t active_leaf_count;
    bool closed;
    bool cancelled;
    bool done;
    bool errored;
} cflow_statechart_instance_stats;

typedef struct cflow_statechart_instance {
    void *impl;
} cflow_statechart_instance;

typedef struct cflow_statechart_selection_trigger {
    cflow_statechart_trigger_kind kind;
    /** Required only for EVENT; borrowed for this selection call. */
    const cflow_event_view *event;
    /** Required only for COMPLETION. */
    cflow_machine_state_id completion;
} cflow_statechart_selection_trigger;

typedef struct cflow_statechart_selection_snapshot {
    /** Borrowed immutable view, invalidated by the next selection or destroy. */
    const cflow_statechart_transition_id *transition_ids;
    size_t transition_count;
    /** One dense-state bitset per transition, at `exit_set_stride` bytes. */
    const unsigned char *exit_sets;
    size_t exit_set_stride;
} cflow_statechart_selection_snapshot;

/*
 * Transactionally initialize one runtime shell. The immutable Statechart,
 * non-manual SerialExecutor, callback functions, and callback user pointers
 * are borrowed until destroy. Initial state bytes and binding rows are copied.
 * Failure leaves `instance` empty and releases every partial allocation.
 */
cflow_statechart_runtime_status cflow_statechart_instance_init(
    cflow_statechart_instance *instance,
    const cflow_statechart_instance_config *config);

/*
 * Compute the exact configured allocation budget before reading binding rows.
 * `control_bytes` includes the instance control block; allocator metadata and
 * the platform mutex's private allocation are intentionally not counted.
 */
cflow_statechart_runtime_status
cflow_statechart_instance_storage_requirements_internal(
    const cflow_statechart *statechart,
    cflow_statechart_storage_requirements *out);

/**
 * Copy the published document-ordered real-state configuration. If capacity
 * is too small, only `out_state_count` changes to the required count; the
 * state array and version remain untouched.
 */
cflow_statechart_snapshot_status
cflow_statechart_instance_copy_configuration(
    const cflow_statechart_instance *instance,
    cflow_machine_state_id *out_states,
    size_t state_capacity,
    size_t *out_state_count,
    uint64_t *out_version);

cflow_machine_state_id cflow_statechart_instance_current_state(
    const cflow_statechart_instance *instance);

bool cflow_statechart_instance_copy_state(
    const cflow_statechart_instance *instance,
    const cmeta_type_desc **out_type,
    void *out_state,
    size_t state_capacity);

bool cflow_statechart_instance_get_stats(
    const cflow_statechart_instance *instance,
    cflow_statechart_instance_stats *out);

const char *cflow_statechart_instance_error(
    const cflow_statechart_instance *instance);

/*
 * Destroy at a quiescent control-plane boundary. Before calling, the caller
 * must prevent new work and ensure no query/admission races with destroy.
 * An empty handle returns OK. If executor idle cannot be observed (including
 * from the same executor callback), this returns WOULD_BLOCK and preserves the
 * handle and every allocation. Only successful idle observation clears/frees.
 */
cflow_statechart_runtime_status cflow_statechart_instance_destroy(
    cflow_statechart_instance *instance);

/*
 * Allocation-free legality check reused by initial entry and future history
 * restoration. `scratch` is caller-owned temporary storage and requires at
 * least ceil(state_count / 8) bytes.
 */
cflow_statechart_configuration_status
cflow_statechart_configuration_validate_internal(
    const cflow_statechart *statechart,
    const cflow_machine_state_id *states,
    size_t state_count,
    unsigned char *scratch,
    size_t scratch_capacity);

/*
 * Allocation-free pure selection for the runtime's sole serial semantic owner.
 * Guards borrow one published state snapshot and the trigger Event for the
 * callback duration. Failure preserves the first owned error and publishes no
 * configuration or extended-state change.
 */
cflow_statechart_runtime_status cflow_statechart_instance_select_internal(
    cflow_statechart_instance *instance,
    const cflow_statechart_selection_trigger *trigger,
    cflow_statechart_selection_snapshot *out);

bool cflow_statechart_selection_exits_internal(
    const cflow_statechart_instance *instance,
    const cflow_statechart_selection_snapshot *selection,
    size_t transition_position,
    cflow_machine_state_id state);

#endif /* CFLOW_STATECHART_RUNTIME_INTERNAL_H */
