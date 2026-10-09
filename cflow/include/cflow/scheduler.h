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

extern const cmeta_type_desc cflow_type_scheduler_stats;
extern const cmeta_type_desc cflow_type_scheduler_stats_ptr;

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
    X(I,FR3,cflow_schedule_result,try_post_after,stateful, \
      &cflow_type_schedule_result,CMETA_ABI_AGGREGATE,CMETA_RESULT_VALUE, \
      (uint64_t,delay_ticks,CMETA_PARAM_IN, \
       &cmeta_type_uint64,CMETA_ABI_SCALAR), \
      (cflow_task_fn,fn,CMETA_PARAM_IN, \
       &cflow_type_task_fn,CMETA_ABI_FUNCTION_POINTER), \
      (void *,user,CMETA_PARAM_IN | CMETA_PARAM_BORROWED | CMETA_PARAM_NULLABLE, \
       &cmeta_type_void_ptr,CMETA_ABI_OBJECT_POINTER)) \
    X(I,FR3,cflow_task_id,post_after,stateful, \
      &cflow_type_task_id,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE, \
      (uint64_t,delay_ticks,CMETA_PARAM_IN, \
       &cmeta_type_uint64,CMETA_ABI_SCALAR), \
      (cflow_task_fn,fn,CMETA_PARAM_IN, \
       &cflow_type_task_fn,CMETA_ABI_FUNCTION_POINTER), \
      (void *,user,CMETA_PARAM_IN | CMETA_PARAM_BORROWED | CMETA_PARAM_NULLABLE, \
       &cmeta_type_void_ptr,CMETA_ABI_OBJECT_POINTER)) \
    X(I,FR1,bool,cancel,stateful, \
      &cmeta_type_bool,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE, \
      (cflow_task_id,id,CMETA_PARAM_IN, \
       &cflow_type_task_id,CMETA_ABI_SCALAR)) \
    X(I,FR0,bool,run_one,stateful, \
      &cmeta_type_bool,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE) \
    X(I,FR0,size_t,run_ready,stateful, \
      &cmeta_type_size,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE) \
    X(I,FR1,size_t,advance,stateful, \
      &cmeta_type_size,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE, \
      (uint64_t,ticks,CMETA_PARAM_IN, \
       &cmeta_type_uint64,CMETA_ABI_SCALAR)) \
    X(I,FR1,size_t,run_until_idle,stateful, \
      &cmeta_type_size,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE, \
      (size_t,max_steps,CMETA_PARAM_IN, \
       &cmeta_type_size,CMETA_ABI_SCALAR)) \
    X(I,FR0,bool,wait_idle,stateful, \
      &cmeta_type_bool,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE) \
    X(I,FR0,uint64_t,now,stateful, \
      &cmeta_type_uint64,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE) \
    X(I,FR0,size_t,pending,stateful, \
      &cmeta_type_size,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE) \
    X(I,FR0,bool,shutdown,stateful, \
      &cmeta_type_bool,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE) \
    X(I,FR1,bool,get_stats,stateful, \
      &cmeta_type_bool,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE, \
      (cflow_scheduler_stats *,out,CMETA_PARAM_OUT | CMETA_PARAM_BORROWED, \
       &cflow_type_scheduler_stats_ptr,CMETA_ABI_OBJECT_POINTER)) \
    X(I,FD0,void,destroy,stateful, \
      &cmeta_type_void,CMETA_ABI_VOID)

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
/** Bind a concurrent-admission, zero-delay Scheduler to an existing
 * owner-affine SerialExecutor. Machine and Subscription work share the SAME
 * underlying bounded Executor queue; no new queue, worker or timer is created.
 *
 * Binding must run on the Executor owner before dispatch starts. The Scheduler
 * borrows the Executor, so destroy Scheduler after closing all Subscriptions
 * and before destroying Executor. ready_capacity bounds the preallocated
 * Scheduler task-ID/cancellation slots and must not exceed the Executor
 * queue capacity.
 *
 * Concurrent callers may post/cancel tasks. Only the captured owner may
 * run_one/run_ready/run_until_idle. Accepted fn callbacks are dispatched
 * on that owner, never inline from admission. cancel(id) may synchronously
 * invoke cancel/finalize on its CALLER's thread, matching Scheduler cancel's
 * borrowed-user lifetime contract; only running callbacks are owner-affine.
 * A successful cancel leaves a bounded tombstone until the owner consumes
 * the corresponding Executor task. Shutdown seals only Scheduler admission,
 * and never shuts down the borrowed Executor (which other Actors may use).
 * Delayed tasks are explicitly unsupported.
 *
 * The host owns progress/wake, must periodically drive a finite quantum of
 * the shared Executor and must quiesce all external post/cancel/wake tails
 * before Scheduler destruction. Destroy must not execute from a callback.
 */
bool cflow_scheduler_owner_bind(cflow_scheduler *scheduler,
                                cflow_executor *owner_executor,
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
