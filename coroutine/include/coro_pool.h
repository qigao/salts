/**
 * @file coro_pool.h
 * @brief Generic bounded coroutine reuse pool for Salts.
 */

#ifndef SALTS_CORO_POOL_H
#define SALTS_CORO_POOL_H

#include "coro.h"
#include <stddef.h>
#include <salts/coroutine_module.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A pool has one control and execution owner at a time and is not thread-safe.
 * Create, acquire, release, abandon, and destroy it from the same shard/thread,
 * or transfer ownership only while no operation or coroutine is active.
 */
typedef struct coro_pool_s coro_pool_t;

typedef void *(*coro_pool_alloc_fn)(void *user_data, size_t size);
typedef void (*coro_pool_free_fn)(void *user_data, void *ptr);

typedef struct {
  size_t initial_capacity;           /**< Initial free entries to create */
  size_t max_capacity;               /**< Maximum entries, 0 = unlimited */
  size_t stack_size;                 /**< Coroutine stack size, 0 = default */
  size_t storage_size;               /**< Coroutine storage size, 0 = default */
  coro_pool_alloc_fn alloc_fn; /**< Optional entry-shell allocator */
  coro_pool_free_fn free_fn;   /**< Optional entry-shell freer */
  void *allocator_data;              /**< Allocator user data */
} coro_pool_config_t;

#define SALTS_CORO_POOL_CONFIG_DEFAULT {16, 1024, 0, 0, NULL, NULL, NULL}

SALTS_COROUTINE_C_API coro_pool_t *
coro_pool_create(const coro_pool_config_t *config);
SALTS_COROUTINE_C_API void coro_pool_destroy(coro_pool_t *pool);

SALTS_COROUTINE_C_API coro_t *coro_pool_acquire(coro_pool_t *pool, coro_fn fn,
                                                      void *arg);
SALTS_COROUTINE_C_API void coro_pool_release(coro_pool_t *pool, coro_t *co);

/**
 * Destroys one non-running frame whose execution cannot be resumed safely and
 * returns its bounded pool entry for reuse. Returns 0 on success and -1 when
 * the frame is running or is not actively owned by pool.
 */
SALTS_COROUTINE_C_API int coro_pool_abandon(coro_pool_t *pool, coro_t *co);

/**
 * @brief Reclaim pool bookkeeping for a coroutine being force-destroyed.
 *
 * This is normally called through the coroutine discard hook registered by
 * coro_pool_acquire().
 */
SALTS_COROUTINE_C_API void coro_pool_discard_coro(coro_t *co);

SALTS_COROUTINE_C_API void coro_pool_forget_active(coro_pool_t *pool);

SALTS_COROUTINE_C_API coro_t *
coro_spawn_pooled(coro_scheduler_t *sched, coro_pool_t *pool, coro_fn fn, void *arg);

SALTS_COROUTINE_C_API size_t coro_pool_free_count(const coro_pool_t *pool);
SALTS_COROUTINE_C_API size_t coro_pool_active_count(const coro_pool_t *pool);
SALTS_COROUTINE_C_API size_t coro_pool_capacity(const coro_pool_t *pool);
SALTS_COROUTINE_C_API size_t coro_pool_retained_count(const coro_pool_t *pool);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_CORO_POOL_H */
