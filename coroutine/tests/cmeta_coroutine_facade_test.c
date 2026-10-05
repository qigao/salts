#include "tinytest.h"

#include <cmeta/coroutine.h>
#include <salts/error_codes.h>
#include <salts/thread.h>

#include <stdatomic.h>

typedef struct cmeta_coroutine_facade_state {
  cmeta_executor *executor;
  cmeta_wait_handle wait;
  atomic_int handle_ready;
  atomic_int resumed;
  int begin_status;
  int yield_status;
  int wait_status;
  int completion_status;
  size_t before_shard;
  size_t after_shard;
} cmeta_coroutine_facade_state;

static int wait_atomic_true(atomic_int *value) {
  for (int round = 0; round < 2000; ++round) {
    if (atomic_load_explicit(value, memory_order_acquire) != 0) return 1;
    salts_sleep_ms(1);
  }
  return 0;
}

static void cmeta_coroutine_facade_task(coro_t *coroutine, void *arg) {
  cmeta_coroutine_facade_state *state =
      (cmeta_coroutine_facade_state *)arg;
  (void)coroutine;

  state->before_shard = cmeta_current_shard(state->executor);
  state->begin_status = cmeta_wait_begin(&state->wait);
  atomic_store_explicit(&state->handle_ready, 1, memory_order_release);
  if (state->begin_status != SALTS_OK) return;

  state->yield_status = cmeta_yield();
  if (state->yield_status != SALTS_OK) return;

  state->wait_status = cmeta_wait(state->wait, &state->completion_status);
  state->after_shard = cmeta_current_shard(state->executor);
  atomic_store_explicit(&state->resumed, 1, memory_order_release);
}

spec("CMeta coroutine facade") {
  it("keeps minicoro private while yield/wait resume on the owner shard") {
    salts_coro_executor_config_t config = SALTS_CORO_EXECUTOR_CONFIG_DEFAULT;
    cmeta_coroutine_facade_state state = {0};
    salts_coro_executor_task_t task = {
        cmeta_coroutine_facade_task, NULL, NULL, &state
    };

    config.worker_count = 1u;
    config.queue_capacity_per_worker = 2u;
    config.coroutine_pool.max_capacity = 2u;

    state.executor = salts_coro_executor_create(&config);
    check_not_null(state.executor);

    check_equal(salts_coro_executor_submit(state.executor, &task), SALTS_OK);
    check_true(wait_atomic_true(&state.handle_ready));

    /* Completion may win before cmeta_wait() starts; the facade preserves the
     * executor's generation-safe early-completion contract. */
    check_equal(cmeta_wait_complete(state.executor, state.wait, 1234), SALTS_OK);
    check_equal(salts_coro_executor_wait(state.executor), SALTS_OK);

    check_equal(state.begin_status, SALTS_OK);
    check_equal(state.yield_status, SALTS_OK);
    check_equal(state.wait_status, SALTS_OK);
    check_equal(state.completion_status, 1234);
    check_equal(state.before_shard, (size_t)0u);
    check_equal(state.after_shard, (size_t)0u);
    check_equal(atomic_load_explicit(&state.resumed, memory_order_acquire), 1);

    check_equal(salts_coro_executor_destroy(state.executor), SALTS_OK);
  }

  it("rejects yield and wait reservation outside an executor coroutine") {
    cmeta_wait_handle wait = {(uintptr_t)7u, 7u, 7u, 7u, 7u};

    check_equal(cmeta_yield(), SALTS_EINVAL);
    check_equal(cmeta_wait_begin(&wait), SALTS_EINVAL);
    check_equal(wait.owner, (uintptr_t)0u);
    check_null(cmeta_current_executor());
  }
}
