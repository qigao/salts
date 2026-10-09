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

extern const cmeta_type_desc cflow_type_task_fn;

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

extern const cmeta_type_desc cflow_type_executor_task;
extern const cmeta_type_desc cflow_type_executor_task_ptr;

struct cflow_executor_control;
extern const cmeta_type_desc cflow_type_executor_control;
extern const cmeta_type_desc cflow_type_executor_control_ptr;

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

extern const cmeta_type_desc cflow_type_executor_shutdown_policy;
extern const cmeta_type_desc cflow_type_executor_post_status;
extern const cmeta_type_desc cflow_type_executor_wait_status;
extern const cmeta_type_desc cflow_type_executor_stats;
extern const cmeta_type_desc cflow_type_executor_stats_ptr;
extern const cmeta_type_desc cflow_type_executor_protocol_stats;
extern const cmeta_type_desc cflow_type_executor_protocol_stats_ptr;

enum {
    CMETA_EXEC_CAP_MANUAL     = 1u << 0,
    CMETA_EXEC_CAP_SERIAL     = 1u << 1,
    CMETA_EXEC_CAP_CONCURRENT = 1u << 2,
    /** Caller-driven on a fixed owner thread, with concurrent bounded posting.
     * OWNER_AFFINE must also advertise MANUAL and SERIAL. Unlike the ordinary
     * Manual Executor, only the captured owner thread may drive callbacks. */
    CMETA_EXEC_CAP_OWNER_AFFINE = 1u << 3
};

#define CMETA_EXECUTOR_METHODS(X,I) \
    X(I,FR2,cflow_admission_status,try_post,stateful, \
      &cflow_type_admission_status,CMETA_ABI_ENUM,CMETA_RESULT_VALUE, \
      (cflow_task_fn,fn,CMETA_PARAM_IN, \
       &cflow_type_task_fn,CMETA_ABI_FUNCTION_POINTER), \
      (void *,user,CMETA_PARAM_IN | CMETA_PARAM_BORROWED | CMETA_PARAM_NULLABLE, \
       &cmeta_type_void_ptr,CMETA_ABI_OBJECT_POINTER)) \
    X(I,FR2,bool,post,stateful, \
      &cmeta_type_bool,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE, \
      (cflow_task_fn,fn,CMETA_PARAM_IN, \
       &cflow_type_task_fn,CMETA_ABI_FUNCTION_POINTER), \
      (void *,user,CMETA_PARAM_IN | CMETA_PARAM_BORROWED | CMETA_PARAM_NULLABLE, \
       &cmeta_type_void_ptr,CMETA_ABI_OBJECT_POINTER)) \
    X(I,FR0,bool,run_one,stateful, \
      &cmeta_type_bool,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE) \
    X(I,FR0,size_t,run_ready,stateful, \
      &cmeta_type_size,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE) \
    X(I,FR0,bool,wait_idle,stateful, \
      &cmeta_type_bool,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE) \
    X(I,FR0,size_t,pending,stateful, \
      &cmeta_type_size,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE) \
    X(I,FR0,bool,shutdown,stateful, \
      &cmeta_type_bool,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE) \
    X(I,FR1,bool,get_stats,stateful, \
      &cmeta_type_bool,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE, \
      (cflow_executor_stats *,out,CMETA_PARAM_OUT | CMETA_PARAM_BORROWED, \
       &cflow_type_executor_stats_ptr,CMETA_ABI_OBJECT_POINTER)) \
    X(I,FD0,void,destroy,stateful, \
      &cmeta_type_void,CMETA_ABI_VOID) \
    X(I,FR1,cflow_admission_status,task_admit,stateful, \
      &cflow_type_admission_status,CMETA_ABI_ENUM,CMETA_RESULT_VALUE, \
      (const cflow_executor_task *,task,CMETA_PARAM_IN | CMETA_PARAM_BORROWED, \
       &cflow_type_executor_task_ptr,CMETA_ABI_OBJECT_POINTER)) \
    X(I,FR1,bool,project_control,stateful, \
      &cmeta_type_bool,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE, \
      (struct cflow_executor_control *,out, \
       CMETA_PARAM_OUT | CMETA_PARAM_BORROWED, \
       &cflow_type_executor_control_ptr,CMETA_ABI_OBJECT_POINTER)) \
    X(I,FR0,bool,is_current,stateful, \
      &cmeta_type_bool,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE)
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
    X(I,FR2,cflow_executor_post_status,post,stateful, \
      &cflow_type_executor_post_status,CMETA_ABI_ENUM,CMETA_RESULT_VALUE, \
      (cflow_task_fn,fn,CMETA_PARAM_IN, \
       &cflow_type_task_fn,CMETA_ABI_FUNCTION_POINTER), \
      (void *,user,CMETA_PARAM_IN | CMETA_PARAM_BORROWED | CMETA_PARAM_NULLABLE, \
       &cmeta_type_void_ptr,CMETA_ABI_OBJECT_POINTER)) \
    X(I,FR0,cflow_executor_wait_status,wait_idle,stateful, \
      &cflow_type_executor_wait_status,CMETA_ABI_ENUM,CMETA_RESULT_VALUE) \
    X(I,FR1,bool,shutdown,stateful, \
      &cmeta_type_bool,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE, \
      (cflow_executor_shutdown_policy,policy,CMETA_PARAM_IN, \
       &cflow_type_executor_shutdown_policy,CMETA_ABI_ENUM)) \
    X(I,FR1,bool,get_stats,stateful, \
      &cmeta_type_bool,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE, \
      (cflow_executor_protocol_stats *,out, \
       CMETA_PARAM_OUT | CMETA_PARAM_BORROWED, \
       &cflow_type_executor_protocol_stats_ptr,CMETA_ABI_OBJECT_POINTER)) \
    X(I,FR1,cflow_executor_post_status,task_post,stateful, \
      &cflow_type_executor_post_status,CMETA_ABI_ENUM,CMETA_RESULT_VALUE, \
      (const cflow_executor_task *,task,CMETA_PARAM_IN | CMETA_PARAM_BORROWED, \
       &cflow_type_executor_task_ptr,CMETA_ABI_OBJECT_POINTER))
CMETA_INTERFACE(cflow_executor_control, CMETA_EXECUTOR_CONTROL_METHODS);

/**
 * Bind the protocol control plane through the executor provider that created
 * the handle. `out` must be zero-initialized. Providers that do not expose a
 * control plane, invalid executors, and a non-empty `out` return false without
 * changing `out`. Provider dispatch remains valid across static-library
 * EXE/DLL boundaries as long as the creating code module remains loaded.
 */
bool cflow_executor_as_control(cflow_executor *executor,
                               cflow_executor_control *out);

/**
 * Attempt non-blocking descriptor admission through the creating Executor
 * provider. The descriptor is copied on success. Providers that do not support
 * descriptor admission return CFLOW_ADMISSION_INVALID_ARGUMENT.
 */
cflow_admission_status cflow_executor_try_post_task(
    cflow_executor *executor, const cflow_executor_task *task);

/**
 * Submit a descriptor through the creating provider's control view.
 * Pool callers may wait for bounded capacity; same-Executor callbacks fail
 * with CFLOW_EXECUTOR_POST_WOULD_BLOCK when waiting would be required.
 */
cflow_executor_post_status cflow_executor_control_post_task(
    cflow_executor_control *control, const cflow_executor_task *task);

/**
 * Initialize an owning Executor handle.
 *
 * `executor` must be zero-initialized. Reinitializing a live handle fails
 * without changing it. Other initialization failures also leave it unchanged,
 * and destroy restores it to the zero state.
 */
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

/**
 * Initialize a fixed-capacity, owner-driven SerialExecutor.
 *
 * The calling thread becomes the only task execution/driver thread. Other
 * threads may post concurrently and receive exact FULL/CLOSED admission.
 * The existing bounded typed CFlow Mailbox stores task descriptors; no worker
 * thread is created. Caller-provided wake, when non-NULL, is invoked only as
 * an advisory signal *after* a successful queue publication (and on shutdown
 * when needed), outside the Executor mutex. It may run on any producer thread,
 * must not call run_one/run_ready or destroy the Executor, and must not block.
 * The host must arm its NativeIO wake before using this hook, drive bounded
 * run_one() quanta, and check pending work before sleeping. A wake failure does
 * NOT change an accepted admission into a rejection.
 *
 * This Executor advertises SERIAL|MANUAL|OWNER_AFFINE. The explicit owner
 * capability permits Machine execution without misrepresenting
 * a generic caller-driven Manual Executor as a worker Executor.
 * Statechart initialization is still synchronous and rejects caller-driven
 * Executors until an owner-safe initial stabilization contract is available. No live task
 * can migrate to another owner. It owns no CNet backend, connection, or loop.
 *
 * Successful descriptor admission copies the descriptor. Accepted callbacks
 * run (or cancel on CANCEL_PENDING shutdown) and finalize once on the owner.
 * Other threads must stop posting, including any post->wake tail, before
 * owner-side destruction. Do not destroy from an Executor callback. Shutdown
 * closes admission; the host must continue owner-driven progress to settle it.
 */
bool cflow_executor_owner_init_with_capacity(
    cflow_executor *executor, size_t capacity,
    cflow_task_fn wake, void *wake_user);

#ifdef __cplusplus
}
#endif
#endif /* CFLOW_EXECUTOR_H */
