#ifndef CFLOW_TIMER_EVENT_INTERNAL_H
#define CFLOW_TIMER_EVENT_INTERNAL_H

#include <cflow/timer_event.h>

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

#endif /* CFLOW_TIMER_EVENT_INTERNAL_H */

