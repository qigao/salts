#ifndef CFLOW_SCHEDULER_INTERNAL_H
#define CFLOW_SCHEDULER_INTERNAL_H

#include <cflow/scheduler.h>

/* Repository-owned Scheduler descriptor path. False means the Scheduler is a
 * foreign implementation and `out` is unchanged. */
bool cflow_scheduler_try_post_task_after_internal(
    cflow_scheduler *scheduler, uint64_t delay_ms,
    const cflow_executor_task *task, cflow_schedule_result *out);

bool cflow_scheduler_worker_try_post_task_after_internal(
    cflow_scheduler *scheduler, uint64_t delay_ms,
    const cflow_executor_task *task, cflow_schedule_result *out);

static inline void cflow_scheduler_settle_cancelled_task_internal(
    const cflow_executor_task *task) {
    if (!task) return;
    if (task->cancel) task->cancel(task->user);
    if (task->finalize) task->finalize(task->user);
}

#endif /* CFLOW_SCHEDULER_INTERNAL_H */
