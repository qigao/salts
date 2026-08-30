#ifndef CFLOW_SCHEDULER_H
#define CFLOW_SCHEDULER_H

#include <cflow/executor.h>
#include <cmeta/interface.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CMETA_SCHED_CAP_DELAYED      = 1u << 0,
    CMETA_SCHED_CAP_MANUAL_CLOCK = 1u << 1,
    CMETA_SCHED_CAP_CONCURRENT   = 1u << 2,
    /** Admission is zero-delay only and work advances only on the caller. */
    CMETA_SCHED_CAP_CALLER_DRIVEN_ZERO_DELAY = 1u << 3
};

typedef struct cflow_scheduler_stats {
    size_t ready_capacity;
    size_t timer_capacity;
    size_t ready_pending;
    size_t timer_pending;
    size_t dispatching;
    size_t peak_pending;
    size_t rejected_full;
    size_t rejected_closed;
    size_t cancelled_on_shutdown;
} cflow_scheduler_stats;

/**
 * Scheduler is an execution facade, not an inheritance hierarchy.
 *
 * Successful admission returns a nonzero task ID and borrows `fn` plus `user`
 * until `fn` returns or `cancel(id)` returns true. A true cancel result means
 * the pending task was removed and `fn` will not execute; false means no such
 * ownership transfer occurred. An implementation may execute `fn` inline
 * before admission returns; the returned ID remains nonzero and later cancel
 * then returns false.
 */
#define CMETA_SCHEDULER_METHODS(X,I) \
    X(I,R3,cflow_schedule_result,try_post_after,uint64_t,delay_ticks,cflow_task_fn,fn,void *,user) \
    X(I,R3,cflow_task_id,post_after,uint64_t,delay_ticks,cflow_task_fn,fn,void *,user) \
    X(I,R1,bool,cancel,cflow_task_id,id) \
    X(I,R0,bool,run_one,_) \
    X(I,R0,size_t,run_ready,_) \
    X(I,R1,size_t,advance,uint64_t,ticks) \
    X(I,R1,size_t,run_until_idle,size_t,max_steps) \
    X(I,R0,bool,wait_idle,_) \
    X(I,R0,uint64_t,now,_) \
    X(I,R0,size_t,pending,_) \
    X(I,R0,bool,shutdown,_) \
    X(I,R1,bool,get_stats,cflow_scheduler_stats *,out) \
    X(I,D0,void,destroy,_)

CMETA_INTERFACE(cflow_scheduler, CMETA_SCHEDULER_METHODS);

/**
 * Initialize an owning Scheduler handle.
 *
 * `scheduler` must be zero-initialized. Reinitializing a live handle fails
 * without changing it. Other initialization failures also leave it unchanged,
 * and destroy restores it to the zero state.
 */
bool cflow_scheduler_test_init(cflow_scheduler *scheduler);
bool cflow_scheduler_test_init_with_capacity(cflow_scheduler *scheduler,
                                             size_t ready_capacity,
                                             size_t timer_capacity);
/**
 * Initialize an owning zero-delay Inline Scheduler.
 *
 * Accepted tasks execute exactly once before admission returns. The Scheduler
 * has no queue or clock: nonzero delays are rejected, cancel never removes an
 * accepted task, and drive methods report no queued work. Callers must
 * serialize access. Task callbacks run on the posting thread and must not
 * destroy this Scheduler before their admission call returns.
 */
bool cflow_scheduler_inline_init(cflow_scheduler *scheduler);
/**
 * Initialize an owning bounded, caller-driven, zero-delay Scheduler.
 *
 * Accepted tasks remain queued until run_one(), run_ready(), or
 * run_until_idle() drives them. Nonzero delays are rejected and cancel does not
 * remove accepted work. Callers must serialize admission, driving, shutdown,
 * and destroy. Pending tasks are finalized by normal drain or cancelled by
 * destroy according to the built-in Manual Executor contract.
 */
bool cflow_scheduler_manual_init(cflow_scheduler *scheduler);
bool cflow_scheduler_manual_init_with_capacity(cflow_scheduler *scheduler,
                                               size_t ready_capacity);
bool cflow_scheduler_worker_init(cflow_scheduler *scheduler, size_t workers);
bool cflow_scheduler_worker_init_with_capacity(cflow_scheduler *scheduler,
                                               size_t workers,
                                               size_t ready_capacity,
                                               size_t timer_capacity);

cflow_task_id cflow_scheduler_post(cflow_scheduler *scheduler,
                                   cflow_task_fn fn,
                                   void *user);
const char *cflow_scheduler_name(const cflow_scheduler *scheduler);

#ifdef __cplusplus
}
#endif
#endif
