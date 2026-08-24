#ifndef CFLOW_MACHINE_RUNTIME_INTERNAL_H
#define CFLOW_MACHINE_RUNTIME_INTERNAL_H

#include <cflow/machine_runtime.h>

bool cflow_machine_instance_timer_payload_capacity(
    const cflow_machine_instance *instance,
    size_t *out_capacity);

cflow_mailbox_status cflow_machine_instance_timer_event_contract(
    const cflow_machine_instance *instance,
    const cflow_event_view *event,
    const cmeta_type_desc **out_canonical_type);

#endif /* CFLOW_MACHINE_RUNTIME_INTERNAL_H */
