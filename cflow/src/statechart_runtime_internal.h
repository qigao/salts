#ifndef CFLOW_STATECHART_RUNTIME_INTERNAL_H
#define CFLOW_STATECHART_RUNTIME_INTERNAL_H

#include <cflow/statechart_runtime.h>

#ifndef CFLOW_STATECHART_MAX_INSTANCE_BYTES
#define CFLOW_STATECHART_MAX_INSTANCE_BYTES 67108864u
#endif
#ifndef CFLOW_STATECHART_ERROR_CAPACITY
#define CFLOW_STATECHART_ERROR_CAPACITY 512u
#endif
#ifndef CFLOW_STATECHART_DEFAULT_INTERNAL_EVENT_CAPACITY
#define CFLOW_STATECHART_DEFAULT_INTERNAL_EVENT_CAPACITY 16u
#endif
#if CFLOW_STATECHART_ERROR_CAPACITY < 1
#error "CFLOW_STATECHART_ERROR_CAPACITY must be greater than zero"
#endif

typedef struct cflow_statechart_storage_requirements {
    size_t external_event_capacity;
    size_t internal_event_capacity;
    size_t completion_capacity;
    size_t control_bytes;
    size_t binding_bytes;
    size_t configuration_bytes;
    size_t history_bitset_bytes;
    size_t history_count_bytes;
    size_t extended_state_bytes;
    size_t index_work_bytes;
    size_t action_scratch_bytes;
    size_t internal_event_bytes;
    size_t external_event_bytes;
    size_t completion_bytes;
    size_t total_bytes;
} cflow_statechart_storage_requirements;

typedef struct cflow_statechart_microstep_stats {
    uint64_t accepted, completed, failed, cancelled, finalized;
    cflow_statechart_runtime_status last_status;
    size_t internal_capacity, internal_pending;
} cflow_statechart_microstep_stats;

typedef struct cflow_statechart_selection_trigger {
    cflow_statechart_trigger_kind kind;
    const cflow_event_view *event;
    cflow_machine_state_id completion;
} cflow_statechart_selection_trigger;

typedef struct cflow_statechart_selection_snapshot {
    const cflow_statechart_transition_id *transition_ids;
    size_t transition_count;
    const unsigned char *exit_sets;
    size_t exit_set_stride;
    uint64_t instance_token, generation, configuration_version;
    cflow_statechart_trigger_kind trigger_kind;
    cflow_event_id event_id;
    cflow_machine_state_id completion;
} cflow_statechart_selection_snapshot;

cflow_statechart_runtime_status
cflow_statechart_instance_storage_requirements_internal(
    const cflow_statechart *statechart, size_t external_event_capacity,
    size_t internal_event_capacity, size_t completion_capacity,
    cflow_statechart_storage_requirements *out);
cflow_statechart_configuration_status
cflow_statechart_configuration_validate_internal(
    const cflow_statechart *statechart,
    const cflow_machine_state_id *states, size_t state_count,
    unsigned char *scratch, size_t scratch_capacity);
cflow_statechart_runtime_status cflow_statechart_instance_select_internal(
    cflow_statechart_instance *instance,
    const cflow_statechart_selection_trigger *trigger,
    cflow_statechart_selection_snapshot *out);
cflow_admission_status cflow_statechart_instance_try_microstep_internal(
    cflow_statechart_instance *instance,
    const cflow_statechart_selection_trigger *trigger,
    const cflow_statechart_selection_snapshot *selection);
bool cflow_statechart_instance_get_microstep_stats_internal(
    const cflow_statechart_instance *instance,
    cflow_statechart_microstep_stats *out);
cflow_statechart_runtime_status
cflow_statechart_instance_copy_internal_event_internal(
    const cflow_statechart_instance *instance, size_t position,
    cflow_event_id *out_id, const cmeta_type_desc **out_type,
    void *out_payload, size_t payload_capacity);
cflow_statechart_snapshot_status cflow_statechart_instance_copy_history_internal(
    const cflow_statechart_instance *instance,
    cflow_machine_state_id history,
    cflow_machine_state_id *out_states, size_t state_capacity,
    size_t *out_state_count);
bool cflow_statechart_selection_exits_internal(
    const cflow_statechart_instance *instance,
    const cflow_statechart_selection_snapshot *selection,
    size_t transition_position, cflow_machine_state_id state);

#endif
