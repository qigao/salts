/**
 * @file turbo_thread.h
 * @brief Core compatibility surface for threading and thread-pool APIs.
 */

#ifndef TURBO_THREAD_H
#define TURBO_THREAD_H

#include "platform.h"
#include <turbo/thread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Core-owned synchronization policy. */
TURBO_C_API void turbo_sync_set_single_threaded(int enabled);
TURBO_C_API int turbo_sync_is_single_threaded(void);

/* Thread pool remains Core-owned until the Concurrency migration. */
typedef struct turbo_threadpool_s turbo_threadpool_t;
typedef void (*turbo_task_fn)(void *arg);

typedef struct {
  int num_threads;
  size_t queue_capacity;
} turbo_threadpool_config_t;

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
} turbo_threadpool_stats_t;

TURBO_C_API turbo_threadpool_t *turbo_threadpool_create(int num_threads);
TURBO_C_API turbo_threadpool_t *
turbo_threadpool_create_with_config(const turbo_threadpool_config_t *config);
TURBO_C_API void turbo_threadpool_destroy(turbo_threadpool_t *pool);
TURBO_C_API int turbo_threadpool_submit(turbo_threadpool_t *pool,
                                        turbo_task_fn task,
                                        void *arg);
TURBO_C_API int turbo_threadpool_try_submit(turbo_threadpool_t *pool,
                                            turbo_task_fn task,
                                            void *arg);
TURBO_C_API void turbo_threadpool_wait(turbo_threadpool_t *pool);
TURBO_C_API void turbo_threadpool_shutdown(turbo_threadpool_t *pool);
TURBO_C_API int turbo_threadpool_pending(turbo_threadpool_t *pool);
TURBO_C_API int turbo_threadpool_size(turbo_threadpool_t *pool);
TURBO_C_API size_t turbo_threadpool_capacity(turbo_threadpool_t *pool);
TURBO_C_API int turbo_threadpool_is_accepting(turbo_threadpool_t *pool);
TURBO_C_API void turbo_threadpool_get_stats(turbo_threadpool_t *pool,
                                            turbo_threadpool_stats_t *stats);

TURBO_C_API int turbo_getpid(void);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_THREAD_H */
