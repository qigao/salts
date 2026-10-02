#ifndef SALTS_CORO_EXECUTOR_INTERNAL_H
#define SALTS_CORO_EXECUTOR_INTERNAL_H

#include "salts_coro_executor.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Internal benchmark/implementation hook.
 *
 * This is deliberately not installed or exported as public Coroutine API.
 * It atomically admits one all-or-none bounded task range to an explicit shard
 * using the executor's existing queue. Rejection invokes no callbacks and
 * transfers no task ownership.
 */
int salts_coro_executor_try_submit_batch_to_internal(
    salts_coro_executor_t *executor,
    size_t shard,
    const salts_coro_executor_task_t *tasks,
    size_t count);

enum { SALTS_CORO_EXECUTOR_INTERNAL_MAX_DEQUEUE_BATCH = 128u };

/*
 * Internal benchmark/implementation hook for Phase B.
 *
 * The public/default executor keeps a dequeue limit of 1. A private consumer
 * benchmark may raise one shard's limit up to the fixed internal maximum so
 * the worker can copy and release a contiguous FIFO range under one mutex.
 */
int salts_coro_executor_set_dequeue_batch_limit_internal(
    salts_coro_executor_t *executor,
    size_t shard,
    size_t limit);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_CORO_EXECUTOR_INTERNAL_H */
