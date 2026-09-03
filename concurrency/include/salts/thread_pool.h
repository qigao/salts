#ifndef SALTS_THREAD_POOL_H
#define SALTS_THREAD_POOL_H

#include <salts/concurrency.h>
#include <salts/error_codes.h>
#include <stddef.h>
#include <stdint.h>

typedef struct salts_threadpool_s salts_threadpool_t;
typedef void (*salts_task_fn)(void *arg);

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
typedef struct salts_threadpool_task_s {
  salts_task_fn run;
  salts_task_fn cancel;
  salts_task_fn finalize;
  void *arg;
} salts_threadpool_task_t;

typedef struct {
  int num_threads;
  /* Maximum queued work; running callbacks do not consume this capacity. */
  size_t queue_capacity;
} salts_threadpool_config_t;

typedef enum salts_threadpool_shutdown_policy {
  SALTS_THREADPOOL_SHUTDOWN_DRAIN = 0,
  SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING
} salts_threadpool_shutdown_policy_t;

typedef struct {
  int num_threads;
  /* Configured queued-work limit, excluding active callbacks. */
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
} salts_threadpool_stats_t;

SALTS_CONCURRENCY_C_API salts_threadpool_t *salts_threadpool_create(int num_threads);
SALTS_CONCURRENCY_C_API salts_threadpool_t *
salts_threadpool_create_with_config(const salts_threadpool_config_t *config);
SALTS_CONCURRENCY_C_API void salts_threadpool_destroy(salts_threadpool_t *pool);
/**
 * Submit a copied descriptor, waiting for bounded queue space if necessary.
 *
 * @return SALTS_OK, SALTS_EINVAL for an invalid pool/descriptor/run callback,
 * SALTS_ESHUTDOWN when admission is closed, or SALTS_EBUSY when a callback on
 * this pool would have to wait for the same saturated pool to make progress.
 */
SALTS_CONCURRENCY_C_API int salts_threadpool_submit_task(
    salts_threadpool_t *pool, const salts_threadpool_task_t *task);
/**
 * Attempt to submit a copied descriptor without waiting for queue space.
 *
 * @return SALTS_OK, SALTS_EINVAL for an invalid pool/descriptor/run callback,
 * SALTS_ENOBUFS when the queue is full, or SALTS_ESHUTDOWN when admission is
 * closed.
 */
SALTS_CONCURRENCY_C_API int salts_threadpool_try_submit_task(
    salts_threadpool_t *pool, const salts_threadpool_task_t *task);
/**
 * Submit a task, waiting for bounded queue space when necessary.
 *
 * @return SALTS_OK, SALTS_EINVAL for invalid arguments, SALTS_ESHUTDOWN when
 * the pool no longer accepts work, or SALTS_EBUSY when a callback on this pool
 * would have to wait for the same saturated pool to make progress.
 */
SALTS_CONCURRENCY_C_API int salts_threadpool_submit(salts_threadpool_t *pool,
                                                    salts_task_fn task,
                                                    void *arg);
/**
 * Attempt to submit a task without waiting for bounded queue space.
 *
 * @return SALTS_OK, SALTS_EINVAL for invalid arguments, SALTS_ENOBUFS when
 * the queue is full, or SALTS_ESHUTDOWN when the pool no longer accepts work.
 */
SALTS_CONCURRENCY_C_API int salts_threadpool_try_submit(salts_threadpool_t *pool,
                                                        salts_task_fn task,
                                                        void *arg);
/** Wait for accepted work to settle, rejecting a wait from this pool's callback. */
SALTS_CONCURRENCY_C_API int
salts_threadpool_wait_status(salts_threadpool_t *pool);
SALTS_CONCURRENCY_C_API void salts_threadpool_wait(salts_threadpool_t *pool);
/**
 * End admission and either drain queued callbacks or settle them as cancelled.
 * Repeating the selected policy is idempotent; changing it returns SALTS_EBUSY.
 */
SALTS_CONCURRENCY_C_API int salts_threadpool_shutdown_with_policy(
    salts_threadpool_t *pool, salts_threadpool_shutdown_policy_t policy);
SALTS_CONCURRENCY_C_API void salts_threadpool_shutdown(salts_threadpool_t *pool);
SALTS_CONCURRENCY_C_API int salts_threadpool_pending(salts_threadpool_t *pool);
SALTS_CONCURRENCY_C_API int64_t
salts_threadpool_cancelled(salts_threadpool_t *pool);
SALTS_CONCURRENCY_C_API int salts_threadpool_size(salts_threadpool_t *pool);
SALTS_CONCURRENCY_C_API size_t salts_threadpool_capacity(salts_threadpool_t *pool);
SALTS_CONCURRENCY_C_API int salts_threadpool_is_accepting(salts_threadpool_t *pool);
SALTS_CONCURRENCY_C_API void salts_threadpool_get_stats(salts_threadpool_t *pool,
                                                        salts_threadpool_stats_t *stats);

#endif /* SALTS_THREAD_POOL_H */
