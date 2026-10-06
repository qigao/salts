#include "tinytest.h"

#include <cmeta/coroutine.h>
#include <salts/error_codes.h>
#include <salts/thread.h>

#include <stdatomic.h>

enum {
  FACADE_TEST_WORKERS = 2,
  FACADE_TEST_OWNER = 1,
  FACADE_TEST_WAIT_ROUNDS = 10000,
  FACADE_TEST_POLL_MS = 1,
  FACADE_TEST_TIMEOUT_MS = 20,
  FACADE_TEST_DEADLINE_MS = 30000
};

typedef struct facade_state {
  cmeta_executor *executor;
  cmeta_wait_handle wait;
  atomic_int handle_ready;
  atomic_int allow_wait;
  atomic_int finalizes;
  uint32_t timeout_ms;
  int abort_before_wait;
  int begin_status;
  int yield_status;
  int abort_status;
  int wait_status;
  int completion_status;
  cmeta_executor *before_executor;
  cmeta_executor *after_executor;
  size_t before_shard;
  size_t after_shard;
} facade_state;

static int wait_atomic_true(atomic_int *value) {
  for (int round = 0; round < FACADE_TEST_WAIT_ROUNDS; ++round) {
    if (atomic_load_explicit(value, memory_order_acquire) != 0) return 1;
    salts_sleep_ms(FACADE_TEST_POLL_MS);
  }
  return 0;
}

static int wait_until_suspended(cmeta_executor *executor) {
  for (int round = 0; round < FACADE_TEST_WAIT_ROUNDS; ++round) {
    salts_coro_executor_stats_t stats = {0};
    salts_coro_executor_get_stats(executor, &stats);
    if (stats.waiting_awaits == 1u) return 1;
    salts_sleep_ms(FACADE_TEST_POLL_MS);
  }
  return 0;
}

static void facade_task(coro_t *coroutine, void *arg) {
  facade_state *state = (facade_state *)arg;
  (void)coroutine;
  state->before_executor = cmeta_current_executor();
  state->before_shard = cmeta_current_shard(state->executor);
  state->begin_status = cmeta_wait_begin(&state->wait);
  atomic_store_explicit(&state->handle_ready, 1, memory_order_release);
  if (state->begin_status != SALTS_OK) return;

  /* The gate fixes completion-before-wait; the waiting counter fixes the
   * opposite order. Scheduling speed cannot choose the tested path. */
  do {
    state->yield_status = cmeta_yield();
    if (state->yield_status != SALTS_OK) return;
  } while (!atomic_load_explicit(&state->allow_wait, memory_order_acquire));

  if (state->abort_before_wait) state->abort_status = cmeta_wait_abort(state->wait);
  if (state->timeout_ms != 0u)
    state->wait_status = cmeta_wait_for(state->wait, state->timeout_ms,
                                       &state->completion_status);
  else
    state->wait_status = cmeta_wait(state->wait, &state->completion_status);
  state->after_executor = cmeta_current_executor();
  state->after_shard = cmeta_current_shard(state->executor);
}

static void facade_finalize(void *arg) {
  facade_state *state = (facade_state *)arg;
  atomic_fetch_add_explicit(&state->finalizes, 1, memory_order_release);
}

typedef struct reuse_state {
  cmeta_executor *executor;
  cmeta_wait_handle first;
  cmeta_wait_handle second;
  cmeta_wait_handle third;
  int first_begin;
  int zero_timeout;
  int zero_timeout_output;
  int null_output;
  int second_begin_busy;
  int failed_begin_cleared;
  int first_abort;
  int second_begin;
  int stale_wait;
  int stale_wait_output;
  int stale_abort;
  int stale_complete;
  int second_complete;
  int second_wait;
  int second_output;
  int third_begin;
  int third_abort;
} reuse_state;

static void reuse_task(coro_t *coroutine, void *arg) {
  reuse_state *state = (reuse_state *)arg;
  cmeta_wait_handle rejected = {0};
  (void)coroutine;
  state->first_begin = cmeta_wait_begin(&state->first);
  if (state->first_begin != SALTS_OK) return;
  state->zero_timeout_output = SALTS_EIO;
  state->zero_timeout = cmeta_wait_for(state->first, 0u, &state->zero_timeout_output);
  state->null_output = cmeta_wait(state->first, NULL);
  state->second_begin_busy = cmeta_wait_begin(&rejected);
  state->failed_begin_cleared = rejected.owner == 0u && rejected.shard == 0u &&
      rejected.slot == 0u && rejected.generation == 0u && rejected.reserved == 0u;
  state->first_abort = cmeta_wait_abort(state->first);
  if (state->first_abort != SALTS_OK) return;
  state->second_begin = cmeta_wait_begin(&state->second);
  if (state->second_begin != SALTS_OK) return;
  state->stale_wait_output = SALTS_EIO;
  state->stale_wait = cmeta_wait(state->first, &state->stale_wait_output);
  state->stale_abort = cmeta_wait_abort(state->first);
  state->stale_complete = cmeta_wait_complete(state->executor, state->first, SALTS_EIO);
  state->second_complete = cmeta_wait_complete(state->executor, state->second, SALTS_ECANCELED);
  if (state->second_complete != SALTS_OK) {
    state->third_abort = cmeta_wait_abort(state->second);
    return;
  }
  state->second_wait = cmeta_wait(state->second, &state->second_output);
  state->third_begin = cmeta_wait_begin(&state->third);
  if (state->third_begin == SALTS_OK) state->third_abort = cmeta_wait_abort(state->third);
}

spec("CMeta coroutine facade") {
  static facade_state state;
  static int submitted;

  before_each() {
    salts_coro_executor_config_t config = SALTS_CORO_EXECUTOR_CONFIG_DEFAULT;
    state = (facade_state){0};
    submitted = 0;
    config.worker_count = FACADE_TEST_WORKERS;
    config.queue_capacity_per_worker = 2u;
    config.coroutine_pool.max_capacity = 1u;
    state.executor = salts_coro_executor_create(&config);
    check_not_null(state.executor);
  }

  after_each() {
    if (state.executor != NULL) {
      atomic_store_explicit(&state.allow_wait, 1, memory_order_release);
      /* Quiesce the test's producer and release a pending wait after a failed
       * assertion before destroy drains and releases the executor. */
      if (submitted && wait_atomic_true(&state.handle_ready))
        (void)cmeta_wait_complete(state.executor, state.wait, SALTS_ECANCELED);
      check_equal(salts_coro_executor_destroy(state.executor), SALTS_OK);
    }
  }

  it("suspends and resumes on a nonzero owner shard after shutdown") {
    salts_coro_executor_task_t task = {facade_task, NULL, facade_finalize, &state};
    check_equal(salts_coro_executor_submit_to(state.executor, FACADE_TEST_OWNER, &task), SALTS_OK);
    submitted = 1;
    check_true(wait_atomic_true(&state.handle_ready));
    atomic_store_explicit(&state.allow_wait, 1, memory_order_release);
    check_true(wait_until_suspended(state.executor));
    check_equal(salts_coro_executor_shutdown(state.executor), SALTS_OK);
    check_equal(cmeta_wait_complete(state.executor, state.wait, SALTS_EINTR), SALTS_OK);
    check_equal(salts_coro_executor_wait(state.executor), SALTS_OK);
    check_equal(state.begin_status, SALTS_OK);
    check_equal(state.yield_status, SALTS_OK);
    check_equal(state.wait_status, SALTS_OK);
    check_equal(state.completion_status, SALTS_EINTR);
    check_true(state.before_executor == state.executor);
    check_true(state.after_executor == state.executor);
    check_equal(state.before_shard, (size_t)FACADE_TEST_OWNER);
    check_equal(state.after_shard, (size_t)FACADE_TEST_OWNER);
    check_equal(atomic_load_explicit(&state.finalizes, memory_order_acquire), 1);
    check_equal(cmeta_wait_complete(state.executor, state.wait, SALTS_OK), SALTS_ENOENT);
    salts_coro_executor_stats_t stats = {0};
    salts_coro_executor_get_stats(state.executor, &stats);
    check_equal(stats.active_tasks, (uint64_t)0u);
    check_equal(stats.active_awaits, (uint64_t)0u);
    check_equal(stats.waiting_awaits, (uint64_t)0u);
  }

  it("preserves early completion and requires consumption when abort loses") {
    salts_coro_executor_task_t task = {facade_task, NULL, facade_finalize, &state};
    state.abort_before_wait = 1;
    check_equal(salts_coro_executor_submit(state.executor, &task), SALTS_OK);
    submitted = 1;
    check_true(wait_atomic_true(&state.handle_ready));
    check_equal(cmeta_wait_complete(state.executor, state.wait, SALTS_ECANCELED), SALTS_OK);
    check_equal(cmeta_wait_complete(state.executor, state.wait, SALTS_EIO), SALTS_EALREADY);
    atomic_store_explicit(&state.allow_wait, 1, memory_order_release);
    check_equal(salts_coro_executor_wait(state.executor), SALTS_OK);
    check_equal(state.abort_status, SALTS_EALREADY);
    check_equal(state.wait_status, SALTS_OK);
    check_equal(state.completion_status, SALTS_ECANCELED);
    check_equal(atomic_load_explicit(&state.finalizes, memory_order_acquire), 1);
  }

  it("routes external completion to a suspended timed wait") {
    salts_coro_executor_task_t task = {facade_task, NULL, facade_finalize, &state};
    state.timeout_ms = FACADE_TEST_DEADLINE_MS;
    check_equal(salts_coro_executor_submit_to(state.executor, FACADE_TEST_OWNER, &task), SALTS_OK);
    submitted = 1;
    check_true(wait_atomic_true(&state.handle_ready));
    atomic_store_explicit(&state.allow_wait, 1, memory_order_release);
    check_true(wait_until_suspended(state.executor));
    check_equal(cmeta_wait_complete(state.executor, state.wait, SALTS_EIO), SALTS_OK);
    check_equal(salts_coro_executor_wait(state.executor), SALTS_OK);
    check_equal(state.wait_status, SALTS_OK);
    check_equal(state.completion_status, SALTS_EIO);
    check_equal(state.after_shard, (size_t)FACADE_TEST_OWNER);
    check_equal(atomic_load_explicit(&state.finalizes, memory_order_acquire), 1);
  }

  it("returns timeout as operation status and rejects late completion") {
    salts_coro_executor_task_t task = {facade_task, NULL, facade_finalize, &state};
    state.timeout_ms = FACADE_TEST_TIMEOUT_MS;
    atomic_store_explicit(&state.allow_wait, 1, memory_order_release);
    check_equal(salts_coro_executor_submit_to(state.executor, FACADE_TEST_OWNER, &task), SALTS_OK);
    submitted = 1;
    check_equal(salts_coro_executor_wait(state.executor), SALTS_OK);
    check_equal(state.begin_status, SALTS_OK);
    check_equal(state.wait_status, SALTS_OK);
    check_equal(state.completion_status, SALTS_ETIMEDOUT);
    check_equal(state.after_shard, (size_t)FACADE_TEST_OWNER);
    check_equal(cmeta_wait_complete(state.executor, state.wait, SALTS_OK), SALTS_ENOENT);
    check_equal(atomic_load_explicit(&state.finalizes, memory_order_acquire), 1);
  }

  it("reuses the sole wait slot without accepting an aborted generation") {
    reuse_state reuse = {0};
    salts_coro_executor_task_t task = {reuse_task, NULL, NULL, &reuse};
    reuse.executor = state.executor;
    check_equal(salts_coro_executor_submit(state.executor, &task), SALTS_OK);
    check_equal(salts_coro_executor_wait(state.executor), SALTS_OK);
    check_equal(reuse.first_begin, SALTS_OK);
    check_equal(reuse.zero_timeout, SALTS_EINVAL);
    check_equal(reuse.zero_timeout_output, 0);
    check_equal(reuse.null_output, SALTS_EINVAL);
    check_equal(reuse.second_begin_busy, SALTS_EBUSY);
    check_true(reuse.failed_begin_cleared);
    check_equal(reuse.first_abort, SALTS_OK);
    check_equal(reuse.second_begin, SALTS_OK);
    check_equal(reuse.first.slot, reuse.second.slot);
    check_not_equal(reuse.first.generation, reuse.second.generation);
    check_equal(reuse.stale_wait, SALTS_ENOENT);
    check_equal(reuse.stale_wait_output, 0);
    check_equal(reuse.stale_abort, SALTS_ENOENT);
    check_equal(reuse.stale_complete, SALTS_ENOENT);
    check_equal(reuse.second_complete, SALTS_OK);
    check_equal(reuse.second_wait, SALTS_OK);
    check_equal(reuse.second_output, SALTS_ECANCELED);
    check_equal(reuse.third_begin, SALTS_OK);
    check_equal(reuse.third_abort, SALTS_OK);
    check_equal(reuse.second.slot, reuse.third.slot);
    check_not_equal(reuse.second.generation, reuse.third.generation);
  }

  it("rejects completion through another executor without consuming the wait") {
    salts_coro_executor_config_t config = SALTS_CORO_EXECUTOR_CONFIG_DEFAULT;
    salts_coro_executor_task_t task = {facade_task, NULL, facade_finalize, &state};
    config.worker_count = 1u;
    config.coroutine_pool.max_capacity = 1u;
    cmeta_executor *other = salts_coro_executor_create(&config);
    check_not_null(other);
    check_equal(salts_coro_executor_submit(state.executor, &task), SALTS_OK);
    submitted = 1;
    check_true(wait_atomic_true(&state.handle_ready));
    int status = cmeta_wait_complete(other, state.wait, SALTS_EIO);
    check_equal(salts_coro_executor_destroy(other), SALTS_OK);
    check_equal(status, SALTS_EINVAL);
    status = SALTS_EIO;
    check_equal(cmeta_wait(state.wait, &status), SALTS_EINVAL);
    check_equal(status, 0);
    check_equal(cmeta_wait_abort(state.wait), SALTS_EINVAL);
    check_equal(cmeta_wait_complete(state.executor, state.wait, SALTS_OK), SALTS_OK);
    atomic_store_explicit(&state.allow_wait, 1, memory_order_release);
    check_equal(salts_coro_executor_wait(state.executor), SALTS_OK);
    check_equal(state.wait_status, SALTS_OK);
    check_equal(state.completion_status, SALTS_OK);
  }

  it("rejects every suspension operation outside an executor coroutine") {
    cmeta_wait_handle wait = {0};
    int status = SALTS_EIO;
    check_equal(cmeta_yield(), SALTS_EINVAL);
    check_equal(cmeta_wait_begin(&wait), SALTS_EINVAL);
    check_equal(cmeta_wait_begin(NULL), SALTS_EINVAL);
    check_equal(wait.owner, (uintptr_t)0u);
    check_equal(cmeta_wait(wait, &status), SALTS_EINVAL);
    check_equal(status, 0);
    status = SALTS_EIO;
    check_equal(cmeta_wait_for(wait, FACADE_TEST_TIMEOUT_MS, &status), SALTS_EINVAL);
    check_equal(status, 0);
    check_equal(cmeta_wait_abort(wait), SALTS_EINVAL);
    check_equal(cmeta_wait_complete(NULL, wait, SALTS_OK), SALTS_EINVAL);
    check_null(cmeta_current_executor());
    check_equal(cmeta_current_shard(state.executor), SIZE_MAX);
    check_equal(cmeta_current_shard(NULL), SIZE_MAX);
  }
}
