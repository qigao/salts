#ifndef CFLOW_EVENT_INTERNAL_H
#define CFLOW_EVENT_INTERNAL_H

#include <cflow/event.h>

bool cflow_mailbox_storage_requirements_internal(
    const cflow_event_type *schema, size_t schema_count, size_t capacity,
    size_t *out_bytes);
cflow_mailbox_status cflow_mailbox_cancel_detach_internal(
    cflow_mailbox *mailbox, cflow_waker *out_waker);

#endif
