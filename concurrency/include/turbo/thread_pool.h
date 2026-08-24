#ifndef TURBO_THREAD_POOL_H
#define TURBO_THREAD_POOL_H

#include <turbo/concurrency.h>
#include <turbo/error_codes.h>
#include <stddef.h>
#include <stdint.h>

typedef struct turbo_threadpool_s turbo_threadpool_t;
typedef void (*turbo_task_fn)(void *arg);

/**
 * Copied task descriptor for exact terminal lifecycle notification.
 *
 * A successful submission invokes exactly one of run or cancel, then invokes
 * finalize when non-NULL. A rejected submission invokes no callback. The arg
 * payload is borrowed until finalize returns, or until run/cancel returns when
 * finalize is NULL. When finalize is present, run/cancel must leave arg valid
 * for it; final ownership release belongs in finalize. All callbacks run in
 * the pool's callback context and must not destroy or synchronously wait on the
 * same pool.
 */
typedef struct turbo_threadpool_task_s {
  turbo_task_fn run;
  turbo_task_fn cancel;
  turbo_task_fn finalize;
  void *arg;
} turbo_threadpool_task_t;

typedef struct {
  int num_threads;
  size_t queue_capacity;
} turbo_threadpool_config_t;

typedef enum turbo_threadpool_shutdown_policy {
  TURBO_THREADPOOL_SHUTDOWN_DRAIN = 0,
  TURBO_THREADPOOL_SHUTDOWN_CANCEL_PENDING
} turbo_threadpool_shutdown_policy_t;

typedef struct {
  int num_threads;
  size_t queue_capacity;
  int accepting;
  int64_t submitted_tasks;
  int64_t started_tasks;
  int64_t completed_tasks;
  int64_t rejected_tasks;
  int64_t queued_tasks;
  int64_t active_tasks;
  int64_t pending_tasks;
  int64_t peak_pending_tasks;
} turbo_threadpool_stats_t;

TURBO_CONCURRENCY_C_API turbo_threadpool_t *turbo_threadpool_create(int num_threads);
TURBO_CONCURRENCY_C_API turbo_threadpool_t *
turbo_threadpool_create_with_config(const turbo_threadpool_config_t *config);
TURBO_CONCURRENCY_C_API void turbo_threadpool_destroy(turbo_threadpool_t *pool);
/**
 * Submit a copied descriptor, waiting for bounded queue space if necessary.
 *
 * @return TURBO_OK, TURBO_EINVAL for an invalid pool/descriptor/run callback,
 * TURBO_ESHUTDOWN when admission is closed, or TURBO_EBUSY when a callback on
 * this pool would have to wait for the same saturated pool to make progress.
 */
TURBO_CONCURRENCY_C_API int turbo_threadpool_submit_task(
    turbo_threadpool_t *pool, const turbo_threadpool_task_t *task);
/**
 * Attempt to submit a copied descriptor without waiting for queue space.
 *
 * @return TURBO_OK, TURBO_EINVAL for an invalid pool/descriptor/run callback,
 * TURBO_ENOBUFS when the queue is full, or TURBO_ESHUTDOWN when admission is
 * closed.
 */
TURBO_CONCURRENCY_C_API int turbo_threadpool_try_submit_task(
    turbo_threadpool_t *pool, const turbo_threadpool_task_t *task);
/**
 * Submit a task, waiting for bounded queue space when necessary.
 *
 * @return TURBO_OK, TURBO_EINVAL for invalid arguments, TURBO_ESHUTDOWN when
 * the pool no longer accepts work, or TURBO_EBUSY when a callback on this pool
 * would have to wait for the same saturated pool to make progress.
 */
TURBO_CONCURRENCY_C_API int turbo_threadpool_submit(turbo_threadpool_t *pool,
                                                    turbo_task_fn task,
                                                    void *arg);
/**
 * Attempt to submit a task without waiting for bounded queue space.
 *
 * @return TURBO_OK, TURBO_EINVAL for invalid arguments, TURBO_ENOBUFS when
 * the queue is full, or TURBO_ESHUTDOWN when the pool no longer accepts work.
 */
TURBO_CONCURRENCY_C_API int turbo_threadpool_try_submit(turbo_threadpool_t *pool,
                                                        turbo_task_fn task,
                                                        void *arg);
/** Wait for accepted work to settle, rejecting a wait from this pool's callback. */
TURBO_CONCURRENCY_C_API int
turbo_threadpool_wait_status(turbo_threadpool_t *pool);
TURBO_CONCURRENCY_C_API void turbo_threadpool_wait(turbo_threadpool_t *pool);
/**
 * End admission and either drain queued callbacks or settle them as cancelled.
 * Repeating the selected policy is idempotent; changing it returns TURBO_EBUSY.
 */
TURBO_CONCURRENCY_C_API int turbo_threadpool_shutdown_with_policy(
    turbo_threadpool_t *pool, turbo_threadpool_shutdown_policy_t policy);
TURBO_CONCURRENCY_C_API void turbo_threadpool_shutdown(turbo_threadpool_t *pool);
TURBO_CONCURRENCY_C_API int turbo_threadpool_pending(turbo_threadpool_t *pool);
TURBO_CONCURRENCY_C_API int64_t
turbo_threadpool_cancelled(turbo_threadpool_t *pool);
TURBO_CONCURRENCY_C_API int turbo_threadpool_size(turbo_threadpool_t *pool);
TURBO_CONCURRENCY_C_API size_t turbo_threadpool_capacity(turbo_threadpool_t *pool);
TURBO_CONCURRENCY_C_API int turbo_threadpool_is_accepting(turbo_threadpool_t *pool);
TURBO_CONCURRENCY_C_API void turbo_threadpool_get_stats(turbo_threadpool_t *pool,
                                                        turbo_threadpool_stats_t *stats);

#endif /* TURBO_THREAD_POOL_H */
