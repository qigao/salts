#ifndef CFLOW_EVENT_INTERNAL_H
#define CFLOW_EVENT_INTERNAL_H

#include <cflow/event.h>

bool cflow_mailbox_storage_requirements_internal(
    const cflow_event_type *schema, size_t schema_count, size_t capacity,
    size_t *out_bytes);

#endif
