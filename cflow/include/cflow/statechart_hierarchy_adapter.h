#ifndef CFLOW_STATECHART_HIERARCHY_ADAPTER_H
#define CFLOW_STATECHART_HIERARCHY_ADAPTER_H

#include <cflow/machine_hierarchy.h>
#include <cflow/statechart_runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cflow_statechart_hierarchy_adapter_status {
    CFLOW_STATECHART_HIERARCHY_ADAPTER_OK = 0,
    CFLOW_STATECHART_HIERARCHY_ADAPTER_INVALID_ARGUMENT,
    CFLOW_STATECHART_HIERARCHY_ADAPTER_LIMIT_EXCEEDED,
    CFLOW_STATECHART_HIERARCHY_ADAPTER_ALLOCATION_FAILED,
    CFLOW_STATECHART_HIERARCHY_ADAPTER_INITIAL_STATE_UNSUPPORTED,
    CFLOW_STATECHART_HIERARCHY_ADAPTER_ERROR_STATE_UNSUPPORTED,
    CFLOW_STATECHART_HIERARCHY_ADAPTER_OBSERVATION_UNSUPPORTED,
    CFLOW_STATECHART_HIERARCHY_ADAPTER_STATECHART_REJECTED
} cflow_statechart_hierarchy_adapter_status;

typedef struct cflow_statechart_hierarchy_adapter_result {
    cflow_statechart_hierarchy_adapter_status status;
    /** Exact nested build result when status is STATECHART_REJECTED. */
    cflow_statechart_status statechart_status;
} cflow_statechart_hierarchy_adapter_result;

/**
 * Build an exclusive Statechart projection from one existing hierarchy.
 *
 * The hierarchy and its flat Machine remain caller-owned and are borrowed only
 * for this call. The destination owns an independent immutable Statechart and
 * retains the hierarchy's borrowed CMeta descriptors until Statechart destroy.
 * The admitted subset excludes ERROR states, observable action outputs, and an
 * initial leaf that differs from the root's declared initial-child chain.
 * Enabled transition traces are preserved. Unhandled Events retain native
 * Statechart behavior (consume without transition), which differs from the
 * legacy Machine runtime's terminal error and is an explicit proof boundary.
 */
cflow_statechart_hierarchy_adapter_result
cflow_statechart_hierarchy_adapter_build(
    cflow_statechart *out,
    const cflow_machine_hierarchy *hierarchy);

typedef struct cflow_statechart_hierarchy_guard_context {
    cflow_machine_guard_fn fn;
    void *user;
} cflow_statechart_hierarchy_guard_context;

typedef struct cflow_statechart_hierarchy_action_context {
    cflow_machine_action_fn fn;
    void *user;
} cflow_statechart_hierarchy_action_context;

/**
 * Adapt one existing Machine guard binding to the Statechart callback shape.
 * `context` and the original callback user must outlive the Statechart instance.
 */
bool cflow_statechart_hierarchy_adapt_guard_binding(
    const cflow_machine_guard_binding *source,
    cflow_statechart_hierarchy_guard_context *context,
    cflow_statechart_guard_binding *out);

/**
 * Adapt one non-observing Machine action binding to a transition executable.
 * `context` and the original callback user must outlive the Statechart instance.
 */
bool cflow_statechart_hierarchy_adapt_action_binding(
    const cflow_machine_action_binding *source,
    cflow_statechart_hierarchy_action_context *context,
    cflow_statechart_executable_binding *out);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_STATECHART_HIERARCHY_ADAPTER_H */
