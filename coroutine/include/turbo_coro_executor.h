/**
 * @file turbo_coro_executor.h
 * @brief Bounded sharded executor for cooperative coroutines.
 */

#ifndef TURBO_UTILS_CORO_EXECUTOR_H
#define TURBO_UTILS_CORO_EXECUTOR_H

#include "turbo_coro_pool.h"

#include <stddef.h>
#include <stdint.h>
#include <turbo/coroutine_module.h>
#include <turbo/error_codes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct turbo_coro_executor_s turbo_coro_executor_t;

/**
 * Copyable generation-checked handle for one executor coroutine suspension.
 *
 * A zero-initialized value is invalid. The handle names executor-owned state;
 * it does not own memory and becomes stale after await returns or abort
 * succeeds.
 */
typedef struct turbo_coro_executor_await_s {
  uintptr_t owner;
  uint32_t shard;
  uint32_t slot;
  uint32_t generation;
  uint32_t reserved;
} turbo_coro_executor_await_t;

typedef void (*turbo_coro_executor_cancel_fn)(void *arg, int status);
typedef void (*turbo_coro_executor_finalize_fn)(void *arg);

/**
 * Copied task descriptor with an exact terminal lifecycle.
 *
 * A successful submission runs the coroutine on one shard, or invokes cancel
 * if the executor cannot create its coroutine frame, and then invokes finalize
 * when non-NULL. A rejected submission invokes no callback. The arg payload is
 * borrowed until finalize returns, or until run/cancel returns when finalize is
 * NULL. Callbacks execute on the selected shard and must not synchronously wait
 * for or destroy the same executor.
 */
typedef struct turbo_coro_executor_task_s {
  coro_fn run;
  turbo_coro_executor_cancel_fn cancel;
  turbo_coro_executor_finalize_fn finalize;
  void *arg;
} turbo_coro_executor_task_t;

typedef struct turbo_coro_executor_config_s {
  /** Worker/shard count; 0 selects the platform CPU count. */
  size_t worker_count;
  /** Exact bounded task-command capacity per shard; must be a power of two. */
  size_t queue_capacity_per_worker;
  /** Private single-owner coroutine pool configuration for every shard. */
  turbo_coro_pool_config_t coroutine_pool;
} turbo_coro_executor_config_t;

#define TURBO_CORO_EXECUTOR_DEFAULT_QUEUE_CAPACITY_PER_WORKER 1024u
#define TURBO_CORO_EXECUTOR_DEFAULT_MAX_COROUTINES_PER_WORKER 64u
#define TURBO_CORO_EXECUTOR_CONFIG_DEFAULT                                                         \
  {                                                                                                \
    0u, TURBO_CORO_EXECUTOR_DEFAULT_QUEUE_CAPACITY_PER_WORKER, {                                   \
      0u, TURBO_CORO_EXECUTOR_DEFAULT_MAX_COROUTINES_PER_WORKER, 0u, 0u, NULL, NULL, NULL          \
    }                                                                                              \
  }

typedef struct turbo_coro_executor_stats_s {
  size_t worker_count;
  size_t queue_capacity_per_worker;
  int accepting;
  uint64_t submitted_tasks;
  uint64_t started_tasks;
  uint64_t completed_tasks;
  uint64_t cancelled_tasks;
  uint64_t rejected_tasks;
  uint64_t queued_tasks;
  uint64_t active_tasks;
  uint64_t peak_queued_tasks;
  uint64_t active_awaits;
  uint64_t waiting_awaits;
} turbo_coro_executor_stats_t;

/**
 * Create an executor and wait until every shard owns a scheduler and pool.
 *
 * The configuration is copied. max_capacity must be nonzero so admission and
 * memory retention remain bounded. The optional pool allocator pair must be
 * supplied together and must be safe for concurrent calls from distinct
 * shards when worker_count is greater than one. With the default minicoro
 * stack and storage, the default retained-frame ceiling is approximately
 * 8.1 MiB per worker, excluding frame metadata and alignment.
 *
 * @param config Copied configuration, or NULL for defaults.
 * @return An owning executor handle, or NULL for invalid configuration,
 * allocation failure, or shard startup failure.
 */
TURBO_COROUTINE_C_API turbo_coro_executor_t *
turbo_coro_executor_create(const turbo_coro_executor_config_t *config);

/**
 * Submit to a round-robin shard, waiting only for that shard's queue space.
 *
 * @param executor Owning executor handle.
 * @param task Descriptor copied on successful admission.
 * @return TURBO_OK, TURBO_EINVAL, TURBO_ESHUTDOWN, or TURBO_EBUSY when a
 * callback on this executor would otherwise wait for saturated queue space.
 */
TURBO_COROUTINE_C_API int turbo_coro_executor_submit(turbo_coro_executor_t *executor,
                                                     const turbo_coro_executor_task_t *task);

/**
 * Try a round-robin submission without waiting for queue space.
 *
 * @param executor Owning executor handle.
 * @param task Descriptor copied on successful admission.
 * @return TURBO_OK, TURBO_EINVAL, TURBO_ESHUTDOWN, or TURBO_ENOBUFS.
 */
TURBO_COROUTINE_C_API int turbo_coro_executor_try_submit(turbo_coro_executor_t *executor,
                                                         const turbo_coro_executor_task_t *task);

/**
 * Submit to an explicit shard, waiting for bounded queue space.
 *
 * Per-shard command order is FIFO. Once started, a coroutine never migrates.
 * A callback on this executor receives TURBO_EBUSY instead of blocking when
 * the selected queue is full.
 *
 * @param executor Owning executor handle.
 * @param shard Zero-based target shard.
 * @param task Descriptor copied on successful admission.
 * @return TURBO_OK, TURBO_EINVAL, TURBO_ESHUTDOWN, or TURBO_EBUSY.
 */
TURBO_COROUTINE_C_API int turbo_coro_executor_submit_to(turbo_coro_executor_t *executor,
                                                        size_t shard,
                                                        const turbo_coro_executor_task_t *task);

/**
 * Try an explicit-shard submission without waiting for queue space.
 *
 * @param executor Owning executor handle.
 * @param shard Zero-based target shard.
 * @param task Descriptor copied on successful admission.
 * @return TURBO_OK, TURBO_EINVAL, TURBO_ESHUTDOWN, or TURBO_ENOBUFS.
 */
TURBO_COROUTINE_C_API int turbo_coro_executor_try_submit_to(turbo_coro_executor_t *executor,
                                                            size_t shard,
                                                            const turbo_coro_executor_task_t *task);

/**
 * Cooperatively yield the currently running executor coroutine.
 *
 * @return TURBO_OK, TURBO_EINVAL outside an executor coroutine, or TURBO_EIO
 *         if the underlying coroutine cannot yield.
 */
TURBO_COROUTINE_C_API int turbo_coro_executor_yield(void);

/**
 * Reserve one bounded await slot for the current executor coroutine.
 *
 * At most one await may be active for a coroutine. Call await after starting
 * the external operation, or abort when operation submission fails.
 *
 * @param out_await Cleared on failure; receives a copyable handle on success.
 * @return TURBO_OK, TURBO_EINVAL outside an executor coroutine, TURBO_EBUSY
 *         for a second active await, or TURBO_ENOBUFS at the shard limit.
 */
TURBO_COROUTINE_C_API int turbo_coro_executor_await_begin(turbo_coro_executor_await_t *out_await);

/**
 * Suspend until the matching completion is routed to this coroutine's shard.
 *
 * Completion may happen before this call; in that case it returns without
 * suspending. The handle is consumed on success. out_status carries the
 * external operation status and is distinct from this function's status.
 *
 * @param await_handle Handle returned by await_begin on this coroutine.
 * @param out_status Receives the status copied by await_complete; cleared on
 *        entry.
 * @return TURBO_OK, TURBO_EINVAL for the wrong coroutine/shard, TURBO_ENOENT
 *         for a stale handle, or TURBO_EIO/TURBO_EPROTO for scheduler failure.
 */
TURBO_COROUTINE_C_API int turbo_coro_executor_await(turbo_coro_executor_await_t await_handle,
                                                    int *out_status);

/**
 * Publish one terminal completion from any external thread.
 *
 * The function never resumes a coroutine on the caller thread. It copies the
 * status into bounded executor storage and signals the owning shard. It remains
 * valid for already accepted awaits after executor shutdown; completion callers
 * must be quiescent before destroy returns.
 *
 * @param executor Executor named by the handle.
 * @param await_handle Copied handle received by the external operation.
 * @param status Terminal status to copy into the await slot.
 * @return TURBO_OK, TURBO_EINVAL for a malformed handle/executor,
 *         TURBO_ENOENT for a stale handle, TURBO_EALREADY for a duplicate
 *         completion, or TURBO_ENOBUFS if the wake-queue invariant is broken.
 */
TURBO_COROUTINE_C_API int
turbo_coro_executor_await_complete(turbo_coro_executor_t *executor,
                                   turbo_coro_executor_await_t await_handle, int status);

/**
 * Release an await whose external operation was not successfully started.
 *
 * Only the bound running coroutine may abort. A completion that already won
 * the race returns TURBO_EALREADY and must be consumed with await.
 *
 * @param await_handle Handle returned by await_begin on this coroutine.
 * @return TURBO_OK, TURBO_EINVAL for the wrong coroutine/shard, TURBO_ENOENT
 *         for a stale handle, TURBO_EALREADY after completion, or TURBO_EBUSY
 *         after suspension.
 */
TURBO_COROUTINE_C_API int turbo_coro_executor_await_abort(turbo_coro_executor_await_t await_handle);

/**
 * Close admission. Accepted tasks are drained; repeated calls are harmless.
 *
 * @param executor Owning executor handle.
 * @return TURBO_OK or TURBO_EINVAL.
 */
TURBO_COROUTINE_C_API int turbo_coro_executor_shutdown(turbo_coro_executor_t *executor);

/**
 * Wait until all currently accepted tasks settle.
 *
 * Use shutdown first when a stable drain boundary is required. Await
 * completion remains accepted after shutdown, but this generic executor does
 * not manufacture terminal results for external operations. A missing
 * completion therefore prevents drain by contract.
 *
 * @param executor Owning executor handle.
 * @return TURBO_OK, TURBO_EINVAL, or TURBO_EBUSY from this executor's callback.
 */
TURBO_COROUTINE_C_API int turbo_coro_executor_wait(turbo_coro_executor_t *executor);

/**
 * Drain and destroy the executor.
 *
 * Destruction is a control-plane operation: external submitters and observers
 * must be quiescent. Calling it from this executor returns TURBO_EBUSY.
 *
 * @param executor Owning executor handle.
 * @return TURBO_OK, TURBO_EINVAL, or TURBO_EBUSY.
 */
TURBO_COROUTINE_C_API int turbo_coro_executor_destroy(turbo_coro_executor_t *executor);

/** @return The executor owning the current shard thread, or NULL otherwise. */
TURBO_COROUTINE_C_API turbo_coro_executor_t *turbo_coro_executor_current(void);

/**
 * @param executor Executor whose current shard is queried.
 * @return The current shard index, or SIZE_MAX outside executor.
 */
TURBO_COROUTINE_C_API size_t
turbo_coro_executor_current_shard(const turbo_coro_executor_t *executor);

/**
 * Copy a concurrent statistics snapshot. Individual counters are atomic, but
 * the aggregate is not a transactional snapshot.
 *
 * @param executor Executor to observe.
 * @param stats Caller-owned output structure.
 */
TURBO_COROUTINE_C_API void turbo_coro_executor_get_stats(const turbo_coro_executor_t *executor,
                                                         turbo_coro_executor_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_UTILS_CORO_EXECUTOR_H */
