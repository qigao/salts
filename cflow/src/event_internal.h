#ifndef CFLOW_EVENT_INTERNAL_H
#define CFLOW_EVENT_INTERNAL_H

#include <cflow/event.h>

/* Commit while a caller's admission gate is held; deliver the detached wake
 * only after releasing that gate. The caller protects mailbox lifetime until
 * the wake returns. */
cflow_mailbox_status cflow_mailbox_try_send_detach_internal(
    cflow_mailbox *mailbox, const cflow_event_view *event, cflow_waker *out_waker);

/* Private accounting observers run under the mailbox lock. They may acquire
 * the Machine instance lock, but cannot call mailbox or external callbacks.
 * This establishes mailbox -> instance lock order for snapshots. */
bool cflow_mailbox_get_stats_observe_internal(
    const cflow_mailbox *mailbox, cflow_mailbox_stats *out,
    void (*observe)(void *user, const cflow_mailbox_stats *stats), void *user);

bool cflow_mailbox_storage_requirements_internal(
    const cflow_event_type *schema, size_t schema_count, size_t capacity,
    size_t *out_bytes);
cflow_mailbox_status cflow_mailbox_cancel_detach_internal(
    cflow_mailbox *mailbox, cflow_waker *out_waker);

#endif
