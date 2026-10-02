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

#ifdef __cplusplus
}
#endif

#endif /* SALTS_CORO_EXECUTOR_INTERNAL_H */
