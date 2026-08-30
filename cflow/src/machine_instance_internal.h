#ifndef CFLOW_MACHINE_INSTANCE_INTERNAL_H
#define CFLOW_MACHINE_INSTANCE_INTERNAL_H

#include <cflow/machine_instance.h>

typedef void (*cflow_machine_transition_commit_hook)(
    void *user,
    /* SIZE_MAX denotes a non-transition terminal/runtime-failure update. */
    size_t normalized_transition_index,
    bool begin);

typedef void (*cflow_machine_commit_boundary_hook)(void *user);

cflow_machine_instance_status cflow_machine_instance_init_internal(
    cflow_machine_instance *instance,
    const cflow_machine_instance_config *config,
    cflow_machine_transition_commit_hook commit_hook,
    void *commit_user,
    cflow_machine_commit_boundary_hook boundary_hook,
    void *boundary_user);

bool cflow_machine_instance_timer_payload_capacity(
    const cflow_machine_instance *instance,
    size_t *out_capacity);

cflow_mailbox_status cflow_machine_instance_timer_event_contract(
    const cflow_machine_instance *instance,
    const cflow_event_view *event,
    const cmeta_type_desc **out_canonical_type);

#endif /* CFLOW_MACHINE_INSTANCE_INTERNAL_H */
