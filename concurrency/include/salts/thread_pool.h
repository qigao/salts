#ifndef SALTS_THREAD_POOL_H
#define SALTS_THREAD_POOL_H

#include <salts/concurrency.h>
#include <salts/error_codes.h>
#include <stddef.h>
#include <stdint.h>

typedef struct cmeta_threadpool_s cmeta_threadpool_t;
typedef void (*cmeta_task_fn)(void *arg);

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
typedef struct cmeta_threadpool_task_s {
  cmeta_task_fn run;
  cmeta_task_fn cancel;
  cmeta_task_fn finalize;
  void *arg;
} cmeta_threadpool_task_t;

typedef struct {
  int num_threads;
  /* Maximum queued work; running callbacks do not consume this capacity. */
  size_t queue_capacity;
} cmeta_threadpool_config_t;

typedef enum cmeta_threadpool_shutdown_policy {
  SALTS_THREADPOOL_SHUTDOWN_DRAIN = 0,
  SALTS_THREADPOOL_SHUTDOWN_CANCEL_PENDING
} cmeta_threadpool_shutdown_policy_t;

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
} cmeta_threadpool_stats_t;

SALTS_CONCURRENCY_C_API cmeta_threadpool_t *cmeta_threadpool_create(int num_threads);
SALTS_CONCURRENCY_C_API cmeta_threadpool_t *
cmeta_threadpool_create_with_config(const cmeta_threadpool_config_t *config);
SALTS_CONCURRENCY_C_API void cmeta_threadpool_destroy(cmeta_threadpool_t *pool);
/**
 * Submit a copied descriptor, waiting for bounded queue space if necessary.
 *
 * @return SALTS_OK, SALTS_EINVAL for an invalid pool/descriptor/run callback,
 * SALTS_ESHUTDOWN when admission is closed, or SALTS_EBUSY when a callback on
 * this pool would have to wait for the same saturated pool to make progress.
 */
SALTS_CONCURRENCY_C_API int cmeta_threadpool_submit_task(
    cmeta_threadpool_t *pool, const cmeta_threadpool_task_t *task);
/**
 * Attempt to submit a copied descriptor without waiting for queue space.
 *
 * @return SALTS_OK, SALTS_EINVAL for an invalid pool/descriptor/run callback,
 * SALTS_ENOBUFS when the queue is full, or SALTS_ESHUTDOWN when admission is
 * closed.
 */
SALTS_CONCURRENCY_C_API int cmeta_threadpool_try_submit_task(
    cmeta_threadpool_t *pool, const cmeta_threadpool_task_t *task);
/**
 * Submit a task, waiting for bounded queue space when necessary.
 *
 * @return SALTS_OK, SALTS_EINVAL for invalid arguments, SALTS_ESHUTDOWN when
 * the pool no longer accepts work, or SALTS_EBUSY when a callback on this pool
 * would have to wait for the same saturated pool to make progress.
 */
SALTS_CONCURRENCY_C_API int cmeta_threadpool_submit(cmeta_threadpool_t *pool,
                                                    cmeta_task_fn task,
                                                    void *arg);
/**
 * Attempt to submit a task without waiting for bounded queue space.
 *
 * @return SALTS_OK, SALTS_EINVAL for invalid arguments, SALTS_ENOBUFS when
 * the queue is full, or SALTS_ESHUTDOWN when the pool no longer accepts work.
 */
SALTS_CONCURRENCY_C_API int cmeta_threadpool_try_submit(cmeta_threadpool_t *pool,
                                                        cmeta_task_fn task,
                                                        void *arg);
/** Wait for accepted work to settle, rejecting a wait from this pool's callback. */
SALTS_CONCURRENCY_C_API int
cmeta_threadpool_wait_status(cmeta_threadpool_t *pool);
SALTS_CONCURRENCY_C_API void cmeta_threadpool_wait(cmeta_threadpool_t *pool);
/**
 * End admission and either drain queued callbacks or settle them as cancelled.
 * Repeating the selected policy is idempotent; changing it returns SALTS_EBUSY.
 */
SALTS_CONCURRENCY_C_API int cmeta_threadpool_shutdown_with_policy(
    cmeta_threadpool_t *pool, cmeta_threadpool_shutdown_policy_t policy);
SALTS_CONCURRENCY_C_API void cmeta_threadpool_shutdown(cmeta_threadpool_t *pool);
SALTS_CONCURRENCY_C_API int cmeta_threadpool_pending(cmeta_threadpool_t *pool);
SALTS_CONCURRENCY_C_API int64_t
cmeta_threadpool_cancelled(cmeta_threadpool_t *pool);
SALTS_CONCURRENCY_C_API int cmeta_threadpool_size(cmeta_threadpool_t *pool);
SALTS_CONCURRENCY_C_API size_t cmeta_threadpool_capacity(cmeta_threadpool_t *pool);
SALTS_CONCURRENCY_C_API int cmeta_threadpool_is_accepting(cmeta_threadpool_t *pool);
SALTS_CONCURRENCY_C_API void cmeta_threadpool_get_stats(cmeta_threadpool_t *pool,
                                                        cmeta_threadpool_stats_t *stats);

#endif /* SALTS_THREAD_POOL_H */
