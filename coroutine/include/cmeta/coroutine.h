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
/**
 * Copyable borrowed token naming one executor-owned wait slot.
 *
 * Zero is invalid. Successful wait/timeout consumption, successful abort, and
 * task return invalidate the generation. The token owns no operation payload.
 * The executor must outlive every producer that can call cmeta_wait_complete.
 */
typedef salts_coro_executor_await_t cmeta_wait_handle;

/** Yield the current coroutine on its owner shard; outside it returns SALTS_EINVAL. */
static inline int cmeta_yield(void) {
  return salts_coro_executor_yield();
}

/**
 * Reserve the current coroutine's sole wait slot into non-NULL out_wait.
 * Returns SALTS_OK, SALTS_EBUSY if already reserved, SALTS_ENOBUFS at capacity,
 * or SALTS_EINVAL outside a coroutine/for NULL. Failure clears out_wait.
 */
static inline int cmeta_wait_begin(cmeta_wait_handle *out_wait) {
  return salts_coro_executor_await_begin(out_wait);
}

/**
 * Consume wait on its original coroutine/shard, suspending if not yet completed.
 * Early completion is preserved. Non-NULL out_status receives the operation's
 * status; SALTS_OK as the function return means successful consumption, even
 * when the operation failed. Invalid context/arguments return SALTS_EINVAL,
 * stale generations SALTS_ENOENT, and backend failures SALTS_EIO/SALTS_EPROTO.
 * A non-NULL out_status is cleared on entry.
 */
static inline int cmeta_wait(cmeta_wait_handle wait, int *out_status) {
  return salts_coro_executor_await(wait, out_status);
}

/**
 * Consume wait after completion or a positive timeout_ms measured from this call.
 * Timeout returns SALTS_OK with *out_status == SALTS_ETIMEDOUT and invalidates
 * the token. Zero timeout returns SALTS_EINVAL without consuming it; otherwise
 * argument/context and output rules match cmeta_wait.
 *
 * Timeout does not stop the external operation or release its borrowed payload.
 * Retain that payload until its owner proves terminal completion/quiescence;
 * a later completion for this consumed token returns SALTS_ENOENT.
 */
static inline int cmeta_wait_for(cmeta_wait_handle wait, uint32_t timeout_ms,
                                 int *out_status) {
  return salts_coro_executor_await_for(wait, timeout_ms, out_status);
}

/**
 * Abort an uncompleted reservation on its original coroutine before suspension.
 * Use when external admission failed. SALTS_OK invalidates wait; SALTS_EALREADY
 * means completion won and the coroutine must consume it with cmeta_wait.
 * Invalid owner/context returns SALTS_EINVAL, stale generations SALTS_ENOENT,
 * and an already suspended reservation SALTS_EBUSY. This never cancels I/O.
 */
static inline int cmeta_wait_abort(cmeta_wait_handle wait) {
  return salts_coro_executor_await_abort(wait);
}

/**
 * Publish status to wait from any thread while executor remains alive.
 * SALTS_OK records one completion; only the owner shard resumes the coroutine.
 * Shutdown still permits accepted waits to complete. Returns SALTS_EINVAL for
 * malformed/cross-executor handles, SALTS_ENOENT for stale ones, SALTS_EALREADY
 * for duplicate completion, or SALTS_ENOBUFS on a broken wake-queue invariant.
 * Quiesce all completion callers before destroying executor.
 */
static inline int cmeta_wait_complete(cmeta_executor *executor,
                                      cmeta_wait_handle wait, int status) {
  return salts_coro_executor_await_complete(executor, wait, status);
}

/**
 * Return the executor owning the current shard thread, or NULL otherwise.
 * Worker context alone does not imply a running coroutine may suspend.
 */
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
