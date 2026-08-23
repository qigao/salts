#ifndef CFLOW_TIMER_QUEUE_H
#define CFLOW_TIMER_QUEUE_H

#include <cflow/executor.h>
#include <cflow/time.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef cflow_task_id cflow_timer_id;

typedef struct cflow_timer_task {
    cflow_timer_id id;
    cflow_deadline deadline;
    uint64_t order;
    cflow_task_fn fn;
    void *user;
} cflow_timer_task;

typedef struct cflow_timer_queue {
    cflow_timer_task *items;
    size_t count;
    size_t capacity;
    cflow_timer_id next_id;
    uint64_t next_order;
} cflow_timer_queue;

bool cflow_timer_queue_init(cflow_timer_queue *queue);
void cflow_timer_queue_destroy(cflow_timer_queue *queue);
cflow_schedule_result cflow_timer_queue_try_schedule(cflow_timer_queue *queue,
                                                     cflow_deadline deadline,
                                                     cflow_task_fn fn,
                                                     void *user);
cflow_timer_id cflow_timer_queue_schedule(cflow_timer_queue *queue,
                                          cflow_deadline deadline,
                                          cflow_task_fn fn,
                                          void *user);
bool cflow_timer_queue_cancel(cflow_timer_queue *queue, cflow_timer_id id);
bool cflow_timer_queue_next_deadline(const cflow_timer_queue *queue,
                                     cflow_deadline *out);
bool cflow_timer_queue_take_ready(cflow_timer_queue *queue,
                                  cflow_instant now,
                                  cflow_timer_task *out);
size_t cflow_timer_queue_pending(const cflow_timer_queue *queue);

#endif /* CFLOW_TIMER_QUEUE_H */
