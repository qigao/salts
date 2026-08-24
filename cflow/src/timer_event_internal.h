#ifndef CFLOW_TIMER_EVENT_INTERNAL_H
#define CFLOW_TIMER_EVENT_INTERNAL_H

#include <cflow/timer_event.h>

#include <cflow/machine.h>

typedef struct cflow_timer_event_claim {
    void *queue_impl;
    void *slot;
} cflow_timer_event_claim;

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

#endif /* CFLOW_TIMER_EVENT_INTERNAL_H */
