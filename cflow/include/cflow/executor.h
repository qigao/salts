#ifndef CFLOW_EXECUTOR_H
#define CFLOW_EXECUTOR_H

#include <cflow/admission.h>
#include <cmeta/interface.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*cflow_task_fn)(void *user);

/**
 * Copied task descriptor for built-in Executor terminal notification.
 *
 * Successful admission invokes exactly one of run or cancel, then invokes
 * finalize when non-NULL. Rejected admission invokes no callback. `user` is
 * borrowed until the final callback returns. When finalize is present,
 * run/cancel must leave `user` valid for it; final ownership release belongs
 * in finalize. Callbacks must not destroy or synchronously wait on the same
 * Executor.
 */
typedef struct cflow_executor_task {
    cflow_task_fn run;
    cflow_task_fn cancel;
    cflow_task_fn finalize;
    void *user;
} cflow_executor_task;

typedef enum cflow_executor_lifecycle {
    CFLOW_EXECUTOR_OPEN = 0,
    CFLOW_EXECUTOR_CLOSING,
    CFLOW_EXECUTOR_CLOSED
} cflow_executor_lifecycle;

typedef enum cflow_executor_shutdown_policy {
    CFLOW_EXECUTOR_SHUTDOWN_DRAIN = 0,
    CFLOW_EXECUTOR_SHUTDOWN_CANCEL_PENDING
} cflow_executor_shutdown_policy;

typedef enum cflow_executor_post_status {
    CFLOW_EXECUTOR_POST_ACCEPTED = 0,
    CFLOW_EXECUTOR_POST_INVALID_ARGUMENT,
    CFLOW_EXECUTOR_POST_FULL,
    CFLOW_EXECUTOR_POST_CLOSED,
    CFLOW_EXECUTOR_POST_WOULD_BLOCK
} cflow_executor_post_status;

typedef enum cflow_executor_wait_status {
    CFLOW_EXECUTOR_WAIT_IDLE = 0,
    CFLOW_EXECUTOR_WAIT_PENDING,
    CFLOW_EXECUTOR_WAIT_INVALID_ARGUMENT,
    CFLOW_EXECUTOR_WAIT_WOULD_BLOCK
} cflow_executor_wait_status;

typedef struct cflow_executor_protocol_stats {
    size_t capacity;
    size_t accepted;
    size_t queued;
    size_t running;
    size_t completed;
    size_t cancelled;
    size_t rejected_full;
    size_t rejected_closed;
    size_t rejected_would_block;
    cflow_executor_lifecycle lifecycle;
} cflow_executor_protocol_stats;

typedef struct cflow_executor_stats {
    size_t capacity;
    size_t pending;
    size_t peak_pending;
    size_t rejected_full;
    size_t rejected_closed;
} cflow_executor_stats;

enum {
    CMETA_EXEC_CAP_MANUAL     = 1u << 0,
    CMETA_EXEC_CAP_SERIAL     = 1u << 1,
    CMETA_EXEC_CAP_CONCURRENT = 1u << 2
};

#define CMETA_EXECUTOR_METHODS(X,I) \
    X(I,R2,cflow_admission_status,try_post,cflow_task_fn,fn,void *,user) \
    X(I,R2,bool,post,cflow_task_fn,fn,void *,user) \
    X(I,R0,bool,run_one,_) \
    X(I,R0,size_t,run_ready,_) \
    X(I,R0,bool,wait_idle,_) \
    X(I,R0,size_t,pending,_) \
    X(I,R0,bool,shutdown,_) \
    X(I,R1,bool,get_stats,cflow_executor_stats *,out) \
    X(I,V0,void,destroy,_)
CMETA_INTERFACE(cflow_executor, CMETA_EXECUTOR_METHODS);

/**
 * Optional protocol control plane for repository-owned Executor backends.
 *
 * post() borrows fn/user until the callback completes or is cancelled and
 * returns an exact admission result. wait_idle() blocks pool callers until all
 * accepted work settles, returns PENDING for an explicitly driven Manual
 * executor, and returns WOULD_BLOCK from the same executor callback. shutdown()
 * closes admission and selects drain or cancel-pending exactly once. get_stats()
 * returns an observational snapshot; after WAIT_IDLE it obeys
 * accepted == completed + cancelled. The control view borrows the executor
 * backend and becomes invalid when the owning cflow_executor is destroyed.
 *
 * Example:
 *   cflow_executor executor = {0};
 *   cflow_executor_control control = {0};
 *   cflow_executor_serial_init(&executor);
 *   cflow_executor_as_control(&executor, &control);
 *   cflow_executor_task descriptor = {
 *       .run = task, .cancel = cancel_task,
 *       .finalize = release_task, .user = user
 *   };
 *   cflow_executor_control_post_task(&control, &descriptor);
 *   cflow_executor_control_shutdown(&control, CFLOW_EXECUTOR_SHUTDOWN_DRAIN);
 *   cflow_executor_control_wait_idle(&control);
 *   cflow_executor_destroy(&executor);
 */
#define CMETA_EXECUTOR_CONTROL_METHODS(X,I) \
    X(I,R2,cflow_executor_post_status,post,cflow_task_fn,fn,void *,user) \
    X(I,R0,cflow_executor_wait_status,wait_idle,_) \
    X(I,R1,bool,shutdown,cflow_executor_shutdown_policy,policy) \
    X(I,R1,bool,get_stats,cflow_executor_protocol_stats *,out)
CMETA_INTERFACE(cflow_executor_control, CMETA_EXECUTOR_CONTROL_METHODS);

/**
 * Bind the protocol control plane to a built-in Manual, Serial, or Worker
 * executor. `out` must be zero-initialized. Custom implementations, invalid
 * executors, and a non-empty `out` return false without changing `out`.
 */
bool cflow_executor_as_control(cflow_executor *executor,
                               cflow_executor_control *out);

/**
 * Attempt non-blocking descriptor admission to a built-in Executor.
 * The descriptor is copied on success; foreign backends return
 * CFLOW_ADMISSION_INVALID_ARGUMENT.
 */
cflow_admission_status cflow_executor_try_post_task(
    cflow_executor *executor, const cflow_executor_task *task);

/**
 * Submit a descriptor through a built-in control view.
 * Pool callers may wait for bounded capacity; same-Executor callbacks fail
 * with CFLOW_EXECUTOR_POST_WOULD_BLOCK when waiting would be required.
 */
cflow_executor_post_status cflow_executor_control_post_task(
    cflow_executor_control *control, const cflow_executor_task *task);

bool cflow_executor_manual_init(cflow_executor *executor);
bool cflow_executor_manual_init_with_capacity(cflow_executor *executor,
                                              size_t capacity);
bool cflow_executor_serial_init(cflow_executor *executor);
bool cflow_executor_serial_init_with_capacity(cflow_executor *executor,
                                              size_t capacity);
bool cflow_executor_worker_init(cflow_executor *executor, size_t workers);
bool cflow_executor_worker_init_with_capacity(cflow_executor *executor,
                                              size_t workers,
                                              size_t capacity);

#ifdef __cplusplus
}
#endif
#endif /* CFLOW_EXECUTOR_H */
