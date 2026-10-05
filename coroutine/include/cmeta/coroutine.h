#ifndef CMETA_COROUTINE_H
#define CMETA_COROUTINE_H

#include <salts_coro_executor.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * CMeta coroutine facade.
 *
 * Application coroutine code uses cmeta_* names; Salts::Coroutine remains the
 * runtime owner and vendor/minicoro remains private behind that module.
 */
typedef salts_coro_executor_t cmeta_executor;
typedef salts_coro_executor_await_t cmeta_wait_handle;

/** Cooperatively yield the current executor coroutine. */
static inline int cmeta_yield(void) {
  return salts_coro_executor_yield();
}

/** Reserve one generation-checked wait slot for the current coroutine. */
static inline int cmeta_wait_begin(cmeta_wait_handle *out_wait) {
  return salts_coro_executor_await_begin(out_wait);
}

/** Suspend until the matching external completion is routed to this shard. */
static inline int cmeta_wait(cmeta_wait_handle wait, int *out_status) {
  return salts_coro_executor_await(wait, out_status);
}

/** Suspend until completion or a relative timeout wins. */
static inline int cmeta_wait_for(cmeta_wait_handle wait, uint32_t timeout_ms,
                                 int *out_status) {
  return salts_coro_executor_await_for(wait, timeout_ms, out_status);
}

/** Abort a reserved wait whose external operation was not admitted. */
static inline int cmeta_wait_abort(cmeta_wait_handle wait) {
  return salts_coro_executor_await_abort(wait);
}

/** Publish one terminal completion from any external thread. */
static inline int cmeta_wait_complete(cmeta_executor *executor,
                                      cmeta_wait_handle wait, int status) {
  return salts_coro_executor_await_complete(executor, wait, status);
}

/** Return the executor owning the current coroutine shard, or NULL. */
static inline cmeta_executor *cmeta_current_executor(void) {
  return salts_coro_executor_current();
}

/** Return the current shard for executor, or SIZE_MAX outside it. */
static inline size_t cmeta_current_shard(const cmeta_executor *executor) {
  return salts_coro_executor_current_shard(executor);
}

#ifdef __cplusplus
}
#endif

#endif /* CMETA_COROUTINE_H */
