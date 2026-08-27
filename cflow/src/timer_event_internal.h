#ifndef CFLOW_TIMER_EVENT_INTERNAL_H
#define CFLOW_TIMER_EVENT_INTERNAL_H

#include <cflow/timer_event.h>

#include <cflow/machine.h>

typedef struct cflow_timer_event_claim {
    void *queue_impl;
    void *slot;
} cflow_timer_event_claim;

typedef cflow_mailbox_status (*cflow_timer_event_contract_fn_internal)(
    void *user, const cflow_event_view *event,
    const cmeta_type_desc **out_canonical_type);
typedef cflow_mailbox_status (*cflow_timer_event_send_fn_internal)(
    void *user, const cflow_event_view *event);

typedef struct cflow_timer_event_target_internal {
    void *user;
    cflow_timer_event_contract_fn_internal contract;
    cflow_timer_event_send_fn_internal send;
} cflow_timer_event_target_internal;

bool cflow_timer_event_queue_storage_requirements_internal(
    size_t payload_capacity,
    size_t capacity,
    size_t *out_bytes);

cflow_timer_event_status cflow_timer_event_queue_init_target_internal(
    cflow_timer_event_queue *queue,
    cflow_clock *clock,
    size_t capacity,
    cflow_timer_event_target_internal target,
    size_t payload_capacity);

/** Internal claim/commit split used by the public one-step operation. */
bool cflow_timer_event_queue_claim_one_ready(
    cflow_timer_event_queue *queue,
    cflow_timer_event_claim *claim,
    cflow_timer_event_fire_result *out_result);

cflow_timer_event_fire_result cflow_timer_event_queue_commit_claim(
    cflow_timer_event_claim *claim);

cflow_timer_event_schedule_result cflow_timer_event_queue_try_schedule_scoped_at(
    cflow_timer_event_queue *queue,
    cflow_deadline deadline,
    const cflow_event_view *event,
    cflow_machine_state_id scope);

cflow_timer_event_schedule_result cflow_timer_event_queue_try_schedule_scoped_after(
    cflow_timer_event_queue *queue,
    cflow_duration delay,
    const cflow_event_view *event,
    cflow_machine_state_id scope);

/** Cancel pending slots in any listed scope; an already FIRING slot wins. */
size_t cflow_timer_event_queue_cancel_scopes(
    cflow_timer_event_queue *queue,
    const cflow_machine_state_id *scopes,
    size_t scope_count);

/** Close admission and cancel pending slots without waiting for FIRING. */
cflow_timer_event_status cflow_timer_event_queue_close_begin_internal(
    cflow_timer_event_queue *queue);

#endif /* CFLOW_TIMER_EVENT_INTERNAL_H */
