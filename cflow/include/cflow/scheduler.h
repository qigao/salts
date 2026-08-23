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
    CMETA_SCHED_CAP_CONCURRENT   = 1u << 2
};

/* Scheduler is a compatibility/runtime facade, not an inheritance hierarchy. */
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
    X(I,V0,void,destroy,_)

CMETA_INTERFACE(cflow_scheduler, CMETA_SCHEDULER_METHODS);

bool cflow_scheduler_test_init(cflow_scheduler *scheduler);
bool cflow_scheduler_test_init_with_capacity(cflow_scheduler *scheduler,
                                             size_t ready_capacity,
                                             size_t timer_capacity);
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
