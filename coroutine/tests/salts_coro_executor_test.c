#include "tinytest.h"

#include <salts/error_codes.h>
#include <salts/thread.h>
#include <salts_coro_executor.h>

#include <stdatomic.h>
#include <stdint.h>

enum {
  EXECUTOR_TEST_WAIT_ROUNDS = 2000,
  EXECUTOR_TEST_PRODUCER_COUNT = 4,
  EXECUTOR_TEST_TASKS_PER_PRODUCER = 100,
  EXECUTOR_TEST_AWAIT_COUNT = 8
};

typedef struct {
  salts_coro_executor_t *executor;
  size_t expected_shard;
  atomic_int *runs;
  atomic_int *migrations;
} affinity_task_state;

typedef struct {
  salts_coro_executor_t *executor;
  atomic_int *shard_hits;
} round_robin_task_state;

typedef struct {
  atomic_int started;
  atomic_int gate;
  atomic_int completed;
} gated_task_state;

typedef struct {
  salts_coro_executor_t *executor;
  salts_coro_executor_task_t task;
  atomic_int entered;
  atomic_int returned;
  int status;
} blocking_submit_state;

typedef struct {
  atomic_int runs;
  atomic_int cancels;
  atomic_int finalizes;
  atomic_int cancel_status;
} lifecycle_task_state;

typedef struct {
  salts_coro_executor_t *executor;
  salts_coro_executor_task_t nested_task;
  atomic_int entered;
  atomic_int proceed;
  atomic_int returned;
  atomic_int status;
} reentrant_submit_state;

typedef struct {
  atomic_int runs;
  atomic_int inherited_user_data;
} frame_isolation_state;

typedef struct {
  salts_coro_executor_t *executor;
  salts_coro_executor_task_t task;
  size_t shard;
  int status;
} producer_state;

typedef struct {
  salts_coro_executor_t *executor;
  salts_coro_executor_await_t await_handle;
  atomic_int handle_ready;
  atomic_int allow_await;
  atomic_int resumed;
  int begin_status;
  int await_status;
  int completion_status;
  size_t before_shard;
  size_t after_shard;
} await_task_state;

typedef struct {
  salts_coro_executor_await_t await_handle;
  atomic_int finished;
  int begin_status;
  int second_begin_status;
  int abort_status;
} await_abort_state;

typedef struct {
  salts_coro_executor_t *executor;
  await_task_state *tasks;
  size_t first;
  size_t stride;
  int status;
} await_completion_producer_state;

static int wait_atomic_at_least(atomic_int *value, int expected) {
  for (int round = 0; round < EXECUTOR_TEST_WAIT_ROUNDS; ++round) {
    if (atomic_load_explicit(value, memory_order_acquire) >= expected) return 1;
    salts_sleep_ms(1);
  }
  return 0;
}

static void affinity_task(coro_t *coroutine, void *arg) {
  affinity_task_state *state = (affinity_task_state *)arg;
  const size_t before = salts_coro_executor_current_shard(state->executor);
  (void)coroutine;

  if (before != state->expected_shard)
    atomic_fetch_add_explicit(state->migrations, 1, memory_order_relaxed);
  coro_yield();
  if (salts_coro_executor_current_shard(state->executor) != before)
    atomic_fetch_add_explicit(state->migrations, 1, memory_order_relaxed);
  atomic_fetch_add_explicit(state->runs, 1, memory_order_release);
}

static void round_robin_task(coro_t *coroutine, void *arg) {
  round_robin_task_state *state = (round_robin_task_state *)arg;
  const size_t shard = salts_coro_executor_current_shard(state->executor);
  (void)coroutine;

  if (shard < 2u) atomic_fetch_add_explicit(&state->shard_hits[shard], 1, memory_order_release);
}

static void gated_task(coro_t *coroutine, void *arg) {
  gated_task_state *state = (gated_task_state *)arg;
  (void)coroutine;

  atomic_fetch_add_explicit(&state->started, 1, memory_order_release);
  while (atomic_load_explicit(&state->gate, memory_order_acquire) == 0)
    coro_yield();
  atomic_fetch_add_explicit(&state->completed, 1, memory_order_release);
}

static void blocking_submit_thread(void *arg) {
  blocking_submit_state *state = (blocking_submit_state *)arg;
  atomic_store_explicit(&state->entered, 1, memory_order_release);
  state->status = salts_coro_executor_submit_to(state->executor, 0u, &state->task);
  atomic_store_explicit(&state->returned, 1, memory_order_release);
}

static void lifecycle_task(coro_t *coroutine, void *arg) {
  lifecycle_task_state *state = (lifecycle_task_state *)arg;
  (void)coroutine;
  atomic_fetch_add_explicit(&state->runs, 1, memory_order_relaxed);
}

static void lifecycle_cancel(void *arg, int status) {
  lifecycle_task_state *state = (lifecycle_task_state *)arg;
  atomic_store_explicit(&state->cancel_status, status, memory_order_relaxed);
  atomic_fetch_add_explicit(&state->cancels, 1, memory_order_relaxed);
}

static void lifecycle_finalize(void *arg) {
  lifecycle_task_state *state = (lifecycle_task_state *)arg;
  atomic_fetch_add_explicit(&state->finalizes, 1, memory_order_release);
}

static void reentrant_submit_task(coro_t *coroutine, void *arg) {
  reentrant_submit_state *state = (reentrant_submit_state *)arg;
  (void)coroutine;

  atomic_store_explicit(&state->entered, 1, memory_order_release);
  while (atomic_load_explicit(&state->proceed, memory_order_acquire) == 0)
    coro_yield();
  atomic_store_explicit(&state->status,
                        salts_coro_executor_submit_to(state->executor, 0u, &state->nested_task),
                        memory_order_relaxed);
  atomic_store_explicit(&state->returned, 1, memory_order_release);
}

static void frame_isolation_task(coro_t *coroutine, void *arg) {
  frame_isolation_state *state = (frame_isolation_state *)arg;
  if (coro_get_data(coroutine) != NULL)
    atomic_fetch_add_explicit(&state->inherited_user_data, 1, memory_order_relaxed);
  coro_set_data(coroutine, state);
  atomic_fetch_add_explicit(&state->runs, 1, memory_order_release);
}

static void producer_thread(void *arg) {
  producer_state *state = (producer_state *)arg;
  state->status = SALTS_OK;
  for (int index = 0; index < EXECUTOR_TEST_TASKS_PER_PRODUCER; ++index) {
    state->status = salts_coro_executor_submit_to(state->executor, state->shard, &state->task);
    if (state->status != SALTS_OK) return;
  }
}

static void await_task(coro_t *coroutine, void *arg) {
  await_task_state *state = (await_task_state *)arg;
  (void)coroutine;

  state->before_shard = salts_coro_executor_current_shard(state->executor);
  state->begin_status = salts_coro_executor_await_begin(&state->await_handle);
  atomic_store_explicit(&state->handle_ready, 1, memory_order_release);
  if (state->begin_status != SALTS_OK) return;
  while (atomic_load_explicit(&state->allow_await, memory_order_acquire) == 0)
    salts_coro_executor_yield();
  state->await_status = salts_coro_executor_await(state->await_handle, &state->completion_status);
  state->after_shard = salts_coro_executor_current_shard(state->executor);
  atomic_store_explicit(&state->resumed, 1, memory_order_release);
}

static void await_abort_task(coro_t *coroutine, void *arg) {
  await_abort_state *state = (await_abort_state *)arg;
  (void)coroutine;

  state->begin_status = salts_coro_executor_await_begin(&state->await_handle);
  if (state->begin_status == SALTS_OK) {
    salts_coro_executor_await_t rejected = {0};
    state->second_begin_status = salts_coro_executor_await_begin(&rejected);
    state->abort_status = salts_coro_executor_await_abort(state->await_handle);
  }
  atomic_store_explicit(&state->finished, 1, memory_order_release);
}

static void await_unconsumed_task(coro_t *coroutine, void *arg) {
  await_abort_state *state = (await_abort_state *)arg;
  (void)coroutine;

  state->begin_status = salts_coro_executor_await_begin(&state->await_handle);
  atomic_store_explicit(&state->finished, 1, memory_order_release);
}

static void await_completion_producer(void *arg) {
  await_completion_producer_state *state = (await_completion_producer_state *)arg;
  state->status = SALTS_OK;
  for (size_t index = state->first; index < EXECUTOR_TEST_AWAIT_COUNT; index += state->stride) {
    state->status = salts_coro_executor_await_complete(
        state->executor, state->tasks[index].await_handle, (int)(100u + index));
    if (state->status != SALTS_OK) return;
  }
}

static int wait_for_waiting_awaits(salts_coro_executor_t *executor, uint64_t expected) {
  for (int round = 0; round < EXECUTOR_TEST_WAIT_ROUNDS; ++round) {
    salts_coro_executor_stats_t stats = {0};
    salts_coro_executor_get_stats(executor, &stats);
    if (stats.waiting_awaits == expected) return 1;
    salts_sleep_ms(1);
  }
  return 0;
}

static void *always_fail_alloc(void *user_data, size_t size) {
  (void)user_data;
  (void)size;
  return NULL;
}

static void no_op_free(void *user_data, void *ptr) {
  (void)user_data;
  (void)ptr;
}

static salts_coro_executor_t *create_single_slot_executor(void) {
  salts_coro_executor_config_t config = SALTS_CORO_EXECUTOR_CONFIG_DEFAULT;
  config.worker_count = 1u;
  config.queue_capacity_per_worker = 1u;
  config.coroutine_pool.initial_capacity = 0u;
  config.coroutine_pool.max_capacity = 1u;
  return salts_coro_executor_create(&config);
}

spec("Salts coroutine executor") {
  it("rejects yield and await reservation outside an executor coroutine") {
    salts_coro_executor_await_t await_handle = {(uintptr_t)9u, 9u, 9u, 9u, 9u};

    check_equal(salts_coro_executor_yield(), SALTS_EINVAL);
    check_equal(salts_coro_executor_await_begin(&await_handle), SALTS_EINVAL);
    check_equal(await_handle.owner, (uintptr_t)0);
    check_equal(await_handle.shard, (uint32_t)0);
    check_equal(await_handle.slot, (uint32_t)0);
    check_equal(await_handle.generation, (uint32_t)0);
  }

  it("routes an external completion to the same shard after shutdown") {
    salts_coro_executor_config_t config = SALTS_CORO_EXECUTOR_CONFIG_DEFAULT;
    salts_coro_executor_t *executor;
    salts_coro_executor_t *other_executor;
    await_task_state state = {0};
    salts_coro_executor_task_t task = {await_task, NULL, NULL, &state};
    salts_coro_executor_stats_t stats = {0};

    config.worker_count = 2u;
    config.queue_capacity_per_worker = 2u;
    config.coroutine_pool.max_capacity = 2u;
    executor = salts_coro_executor_create(&config);
    check_not_null(executor);
    other_executor = create_single_slot_executor();
    check_not_null(other_executor);
    state.executor = executor;
    check_equal(salts_coro_executor_submit_to(executor, 1u, &task), SALTS_OK);
    check_true(wait_atomic_at_least(&state.handle_ready, 1));
    check_equal(salts_coro_executor_await_complete(other_executor, state.await_handle, SALTS_OK),
                SALTS_EINVAL);
    check_equal(salts_coro_executor_destroy(other_executor), SALTS_OK);
    atomic_store_explicit(&state.allow_await, 1, memory_order_release);
    check_true(wait_for_waiting_awaits(executor, 1u));
    check_equal(salts_coro_executor_shutdown(executor), SALTS_OK);
    check_equal(salts_coro_executor_await_complete(executor, state.await_handle, SALTS_ETIMEDOUT),
                SALTS_OK);
    check_equal(salts_coro_executor_wait(executor), SALTS_OK);
    salts_coro_executor_get_stats(executor, &stats);

    check_equal(state.begin_status, SALTS_OK);
    check_equal(state.await_status, SALTS_OK);
    check_equal(state.completion_status, SALTS_ETIMEDOUT);
    check_equal(state.before_shard, (size_t)1);
    check_equal(state.after_shard, (size_t)1);
    check_equal(atomic_load_explicit(&state.resumed, memory_order_acquire), 1);
    check_equal(stats.active_awaits, (uint64_t)0);
    check_equal(stats.waiting_awaits, (uint64_t)0);
    check_equal(salts_coro_executor_destroy(executor), SALTS_OK);
  }

  it("consumes completion published before await without suspending") {
    salts_coro_executor_t *executor = create_single_slot_executor();
    await_task_state state = {0};
    salts_coro_executor_task_t task = {await_task, NULL, NULL, &state};

    check_not_null(executor);
    state.executor = executor;
    check_equal(salts_coro_executor_submit(executor, &task), SALTS_OK);
    check_true(wait_atomic_at_least(&state.handle_ready, 1));
    check_equal(salts_coro_executor_await_complete(executor, state.await_handle, SALTS_ECANCELED),
                SALTS_OK);
    check_equal(salts_coro_executor_await_complete(executor, state.await_handle, SALTS_OK),
                SALTS_EALREADY);
    atomic_store_explicit(&state.allow_await, 1, memory_order_release);
    check_equal(salts_coro_executor_wait(executor), SALTS_OK);

    check_equal(state.begin_status, SALTS_OK);
    check_equal(state.await_status, SALTS_OK);
    check_equal(state.completion_status, SALTS_ECANCELED);
    check_equal(atomic_load_explicit(&state.resumed, memory_order_acquire), 1);
    check_equal(salts_coro_executor_await_complete(executor, state.await_handle, SALTS_OK),
                SALTS_ENOENT);
    check_equal(salts_coro_executor_destroy(executor), SALTS_OK);
  }

  it("accepts concurrent completion producers on one bounded wake queue") {
    salts_coro_executor_config_t config = SALTS_CORO_EXECUTOR_CONFIG_DEFAULT;
    salts_coro_executor_t *executor;
    await_task_state states[EXECUTOR_TEST_AWAIT_COUNT] = {0};
    salts_coro_executor_task_t tasks[EXECUTOR_TEST_AWAIT_COUNT];
    await_completion_producer_state producers[EXECUTOR_TEST_PRODUCER_COUNT] = {0};
    salts_thread_t threads[EXECUTOR_TEST_PRODUCER_COUNT] = {0};

    config.worker_count = 1u;
    config.queue_capacity_per_worker = EXECUTOR_TEST_AWAIT_COUNT;
    config.coroutine_pool.max_capacity = EXECUTOR_TEST_AWAIT_COUNT;
    executor = salts_coro_executor_create(&config);
    check_not_null(executor);
    for (size_t index = 0u; index < EXECUTOR_TEST_AWAIT_COUNT; ++index) {
      states[index].executor = executor;
      tasks[index] = (salts_coro_executor_task_t){await_task, NULL, NULL, &states[index]};
      check_equal(salts_coro_executor_submit(executor, &tasks[index]), SALTS_OK);
    }
    for (size_t index = 0u; index < EXECUTOR_TEST_AWAIT_COUNT; ++index) {
      check_true(wait_atomic_at_least(&states[index].handle_ready, 1));
      atomic_store_explicit(&states[index].allow_await, 1, memory_order_release);
    }
    check_true(wait_for_waiting_awaits(executor, EXECUTOR_TEST_AWAIT_COUNT));

    for (size_t index = 0u; index < EXECUTOR_TEST_PRODUCER_COUNT; ++index) {
      producers[index] = (await_completion_producer_state){executor, states, index,
                                                           EXECUTOR_TEST_PRODUCER_COUNT, SALTS_OK};
      check_equal(
          salts_thread_create(&threads[index], await_completion_producer, &producers[index]),
          SALTS_OK);
    }
    for (size_t index = 0u; index < EXECUTOR_TEST_PRODUCER_COUNT; ++index) {
      check_equal(salts_thread_join(&threads[index]), SALTS_OK);
      check_equal(producers[index].status, SALTS_OK);
    }
    check_equal(salts_coro_executor_wait(executor), SALTS_OK);
    for (size_t index = 0u; index < EXECUTOR_TEST_AWAIT_COUNT; ++index) {
      check_equal(states[index].await_status, SALTS_OK);
      check_equal(states[index].completion_status, (int)(100u + index));
      check_equal(states[index].before_shard, (size_t)0);
      check_equal(states[index].after_shard, (size_t)0);
    }
    check_equal(salts_coro_executor_destroy(executor), SALTS_OK);
  }

  it("aborts an await when external operation submission fails") {
    salts_coro_executor_t *executor = create_single_slot_executor();
    await_abort_state state = {0};
    salts_coro_executor_task_t task = {await_abort_task, NULL, NULL, &state};

    check_not_null(executor);
    check_equal(salts_coro_executor_submit(executor, &task), SALTS_OK);
    check_equal(salts_coro_executor_wait(executor), SALTS_OK);
    check_equal(state.begin_status, SALTS_OK);
    check_equal(state.second_begin_status, SALTS_EBUSY);
    check_equal(state.abort_status, SALTS_OK);
    check_equal(atomic_load_explicit(&state.finished, memory_order_acquire), 1);
    check_equal(salts_coro_executor_await_complete(executor, state.await_handle, SALTS_OK),
                SALTS_ENOENT);
    check_equal(salts_coro_executor_destroy(executor), SALTS_OK);
  }

  it("invalidates an unconsumed await when its task returns") {
    salts_coro_executor_t *executor = create_single_slot_executor();
    await_abort_state state = {0};
    salts_coro_executor_task_t task = {await_unconsumed_task, NULL, NULL, &state};
    salts_coro_executor_stats_t stats = {0};

    check_not_null(executor);
    check_equal(salts_coro_executor_submit(executor, &task), SALTS_OK);
    check_equal(salts_coro_executor_wait(executor), SALTS_OK);
    salts_coro_executor_get_stats(executor, &stats);
    check_equal(state.begin_status, SALTS_OK);
    check_equal(atomic_load_explicit(&state.finished, memory_order_acquire), 1);
    check_equal(stats.active_awaits, (uint64_t)0);
    check_equal(stats.waiting_awaits, (uint64_t)0);
    check_equal(salts_coro_executor_await_complete(executor, state.await_handle, SALTS_OK),
                SALTS_ENOENT);
    check_equal(salts_coro_executor_destroy(executor), SALTS_OK);
  }

  it("keeps an explicitly assigned coroutine on one worker across yield") {
    salts_coro_executor_config_t config = SALTS_CORO_EXECUTOR_CONFIG_DEFAULT;
    salts_coro_executor_t *executor;
    affinity_task_state states[2];
    salts_coro_executor_task_t tasks[2];
    atomic_int runs = 0;
    atomic_int migrations = 0;

    config.worker_count = 2u;
    config.queue_capacity_per_worker = 4u;
    config.coroutine_pool.initial_capacity = 0u;
    config.coroutine_pool.max_capacity = 2u;
    executor = salts_coro_executor_create(&config);
    check_not_null(executor);
    check_equal(salts_coro_executor_current_shard(executor), SIZE_MAX);

    for (size_t shard = 0u; shard < 2u; ++shard) {
      states[shard] = (affinity_task_state){executor, shard, &runs, &migrations};
      tasks[shard] = (salts_coro_executor_task_t){affinity_task, NULL, NULL, &states[shard]};
      check_equal(salts_coro_executor_submit_to(executor, shard, &tasks[shard]), SALTS_OK);
    }

    check_equal(salts_coro_executor_wait(executor), SALTS_OK);
    check_equal(atomic_load_explicit(&runs, memory_order_acquire), 2);
    check_equal(atomic_load_explicit(&migrations, memory_order_acquire), 0);
    check_equal(salts_coro_executor_destroy(executor), SALTS_OK);
  }

  it("round robins unpinned coroutines across workers") {
    salts_coro_executor_config_t config = SALTS_CORO_EXECUTOR_CONFIG_DEFAULT;
    salts_coro_executor_t *executor;
    round_robin_task_state states[4];
    salts_coro_executor_task_t tasks[4];
    atomic_int shard_hits[2] = {0, 0};

    config.worker_count = 2u;
    config.queue_capacity_per_worker = 4u;
    config.coroutine_pool.initial_capacity = 0u;
    config.coroutine_pool.max_capacity = 4u;
    executor = salts_coro_executor_create(&config);
    check_not_null(executor);

    for (size_t index = 0u; index < 4u; ++index) {
      states[index] = (round_robin_task_state){executor, shard_hits};
      tasks[index] = (salts_coro_executor_task_t){round_robin_task, NULL, NULL, &states[index]};
      check_equal(salts_coro_executor_submit(executor, &tasks[index]), SALTS_OK);
    }

    check_equal(salts_coro_executor_wait(executor), SALTS_OK);
    check_equal(atomic_load_explicit(&shard_hits[0], memory_order_acquire), 2);
    check_equal(atomic_load_explicit(&shard_hits[1], memory_order_acquire), 2);
    check_equal(salts_coro_executor_destroy(executor), SALTS_OK);
  }

  it("accepts concurrent MPSC producers on bounded shard queues") {
    salts_coro_executor_config_t config = SALTS_CORO_EXECUTOR_CONFIG_DEFAULT;
    salts_coro_executor_t *executor;
    lifecycle_task_state task_state = {0};
    producer_state producers[EXECUTOR_TEST_PRODUCER_COUNT];
    salts_thread_t threads[EXECUTOR_TEST_PRODUCER_COUNT] = {0};

    config.worker_count = 2u;
    config.queue_capacity_per_worker = 8u;
    config.coroutine_pool.initial_capacity = 0u;
    config.coroutine_pool.max_capacity = 4u;
    executor = salts_coro_executor_create(&config);
    check_not_null(executor);

    for (int index = 0; index < EXECUTOR_TEST_PRODUCER_COUNT; ++index) {
      producers[index].executor = executor;
      producers[index].task = (salts_coro_executor_task_t){lifecycle_task, NULL, NULL, &task_state};
      producers[index].shard = (size_t)index % 2u;
      check_equal(salts_thread_create(&threads[index], producer_thread, &producers[index]),
                  SALTS_OK);
    }
    for (int index = 0; index < EXECUTOR_TEST_PRODUCER_COUNT; ++index) {
      check_equal(salts_thread_join(&threads[index]), SALTS_OK);
      check_equal(producers[index].status, SALTS_OK);
    }

    check_equal(salts_coro_executor_wait(executor), SALTS_OK);
    check_equal(atomic_load_explicit(&task_state.runs, memory_order_acquire),
                EXECUTOR_TEST_PRODUCER_COUNT * EXECUTOR_TEST_TASKS_PER_PRODUCER);
    check_equal(salts_coro_executor_destroy(executor), SALTS_OK);
  }

  it("rejects a try submission when one shard queue is full") {
    salts_coro_executor_t *executor = create_single_slot_executor();
    gated_task_state state = {0};
    salts_coro_executor_task_t task = {gated_task, NULL, NULL, &state};

    check_not_null(executor);
    check_equal(salts_coro_executor_submit_to(executor, 0u, &task), SALTS_OK);
    check_true(wait_atomic_at_least(&state.started, 1));
    check_equal(salts_coro_executor_try_submit_to(executor, 0u, &task), SALTS_OK);
    check_equal(salts_coro_executor_try_submit_to(executor, 0u, &task), SALTS_ENOBUFS);

    atomic_store_explicit(&state.gate, 1, memory_order_release);
    check_equal(salts_coro_executor_wait(executor), SALTS_OK);
    check_equal(atomic_load_explicit(&state.completed, memory_order_acquire), 2);
    check_equal(salts_coro_executor_destroy(executor), SALTS_OK);
  }

  it("wakes a blocked submitter after queue capacity is released") {
    salts_coro_executor_t *executor = create_single_slot_executor();
    gated_task_state state = {0};
    salts_coro_executor_task_t task = {gated_task, NULL, NULL, &state};
    blocking_submit_state submitter = {0};
    salts_thread_t thread = {0};

    check_not_null(executor);
    check_equal(salts_coro_executor_submit_to(executor, 0u, &task), SALTS_OK);
    check_true(wait_atomic_at_least(&state.started, 1));
    check_equal(salts_coro_executor_try_submit_to(executor, 0u, &task), SALTS_OK);

    submitter.executor = executor;
    submitter.task = task;
    check_equal(salts_thread_create(&thread, blocking_submit_thread, &submitter), SALTS_OK);
    check_true(wait_atomic_at_least(&submitter.entered, 1));
    salts_sleep_ms(10);
    check_equal(atomic_load_explicit(&submitter.returned, memory_order_acquire), 0);

    atomic_store_explicit(&state.gate, 1, memory_order_release);
    check_equal(salts_thread_join(&thread), SALTS_OK);
    check_equal(submitter.status, SALTS_OK);
    check_equal(salts_coro_executor_wait(executor), SALTS_OK);
    check_equal(atomic_load_explicit(&state.completed, memory_order_acquire), 3);
    check_equal(salts_coro_executor_destroy(executor), SALTS_OK);
  }

  it("wakes a blocked submitter with shutdown status when admission closes") {
    salts_coro_executor_t *executor = create_single_slot_executor();
    gated_task_state state = {0};
    salts_coro_executor_task_t task = {gated_task, NULL, NULL, &state};
    blocking_submit_state submitter = {0};
    salts_thread_t thread = {0};

    check_not_null(executor);
    check_equal(salts_coro_executor_submit_to(executor, 0u, &task), SALTS_OK);
    check_true(wait_atomic_at_least(&state.started, 1));
    check_equal(salts_coro_executor_try_submit_to(executor, 0u, &task), SALTS_OK);

    submitter.executor = executor;
    submitter.task = task;
    check_equal(salts_thread_create(&thread, blocking_submit_thread, &submitter), SALTS_OK);
    check_true(wait_atomic_at_least(&submitter.entered, 1));
    salts_sleep_ms(10);
    check_equal(atomic_load_explicit(&submitter.returned, memory_order_acquire), 0);

    check_equal(salts_coro_executor_shutdown(executor), SALTS_OK);
    check_equal(salts_thread_join(&thread), SALTS_OK);
    check_equal(submitter.status, SALTS_ESHUTDOWN);
    atomic_store_explicit(&state.gate, 1, memory_order_release);
    check_equal(salts_coro_executor_wait(executor), SALTS_OK);
    check_equal(atomic_load_explicit(&state.completed, memory_order_acquire), 2);
    check_equal(salts_coro_executor_destroy(executor), SALTS_OK);
  }

  it("rejects a blocking reentrant submission instead of deadlocking") {
    salts_coro_executor_t *executor = create_single_slot_executor();
    lifecycle_task_state nested_state = {0};
    reentrant_submit_state state = {0};
    salts_coro_executor_task_t outer_task = {reentrant_submit_task, NULL, NULL, &state};

    check_not_null(executor);
    state.executor = executor;
    state.nested_task = (salts_coro_executor_task_t){lifecycle_task, NULL, NULL, &nested_state};
    check_equal(salts_coro_executor_submit_to(executor, 0u, &outer_task), SALTS_OK);
    check_true(wait_atomic_at_least(&state.entered, 1));
    check_equal(salts_coro_executor_try_submit_to(executor, 0u, &state.nested_task), SALTS_OK);
    atomic_store_explicit(&state.proceed, 1, memory_order_release);

    check_equal(salts_coro_executor_wait(executor), SALTS_OK);
    check_equal(atomic_load_explicit(&state.returned, memory_order_acquire), 1);
    check_equal(atomic_load_explicit(&state.status, memory_order_acquire), SALTS_EBUSY);
    check_equal(atomic_load_explicit(&nested_state.runs, memory_order_acquire), 1);
    check_equal(salts_coro_executor_destroy(executor), SALTS_OK);
  }

  it("drains accepted work and rejects submissions after shutdown") {
    salts_coro_executor_t *executor = create_single_slot_executor();
    gated_task_state state = {0};
    salts_coro_executor_task_t task = {gated_task, NULL, NULL, &state};

    check_not_null(executor);
    atomic_store_explicit(&state.gate, 1, memory_order_release);
    check_equal(salts_coro_executor_submit(executor, &task), SALTS_OK);
    check_equal(salts_coro_executor_shutdown(executor), SALTS_OK);
    check_equal(salts_coro_executor_try_submit(executor, &task), SALTS_ESHUTDOWN);
    check_equal(salts_coro_executor_wait(executor), SALTS_OK);
    check_equal(atomic_load_explicit(&state.completed, memory_order_acquire), 1);
    check_equal(salts_coro_executor_destroy(executor), SALTS_OK);
  }

  it("finalizes an accepted task exactly once after it runs") {
    salts_coro_executor_t *executor = create_single_slot_executor();
    lifecycle_task_state state = {0};
    salts_coro_executor_task_t task = {lifecycle_task, lifecycle_cancel, lifecycle_finalize,
                                       &state};
    salts_coro_executor_stats_t stats = {0};

    check_not_null(executor);
    check_equal(salts_coro_executor_submit(executor, &task), SALTS_OK);
    check_equal(salts_coro_executor_wait(executor), SALTS_OK);
    salts_coro_executor_get_stats(executor, &stats);

    check_equal(atomic_load_explicit(&state.runs, memory_order_acquire), 1);
    check_equal(atomic_load_explicit(&state.cancels, memory_order_acquire), 0);
    check_equal(atomic_load_explicit(&state.finalizes, memory_order_acquire), 1);
    check_equal(stats.submitted_tasks, (uint64_t)1);
    check_equal(stats.started_tasks, (uint64_t)1);
    check_equal(stats.completed_tasks, (uint64_t)1);
    check_equal(stats.cancelled_tasks, (uint64_t)0);
    check_equal(stats.active_tasks, (uint64_t)0);
    check_equal(salts_coro_executor_destroy(executor), SALTS_OK);
  }

  it("does not expose user data retained by a previously pooled task") {
    salts_coro_executor_t *executor = create_single_slot_executor();
    frame_isolation_state state = {0};
    salts_coro_executor_task_t task = {frame_isolation_task, NULL, NULL, &state};

    check_not_null(executor);
    check_equal(salts_coro_executor_submit(executor, &task), SALTS_OK);
    check_equal(salts_coro_executor_submit(executor, &task), SALTS_OK);
    check_equal(salts_coro_executor_wait(executor), SALTS_OK);
    check_equal(atomic_load_explicit(&state.runs, memory_order_acquire), 2);
    check_equal(atomic_load_explicit(&state.inherited_user_data, memory_order_acquire), 0);
    check_equal(salts_coro_executor_destroy(executor), SALTS_OK);
  }

  it("cancels and finalizes exactly once when frame allocation fails") {
    salts_coro_executor_config_t config = SALTS_CORO_EXECUTOR_CONFIG_DEFAULT;
    salts_coro_executor_t *executor;
    lifecycle_task_state state = {0};
    salts_coro_executor_task_t task = {lifecycle_task, lifecycle_cancel, lifecycle_finalize,
                                       &state};
    salts_coro_executor_stats_t stats = {0};

    config.worker_count = 1u;
    config.queue_capacity_per_worker = 1u;
    config.coroutine_pool.initial_capacity = 0u;
    config.coroutine_pool.max_capacity = 1u;
    config.coroutine_pool.alloc_fn = always_fail_alloc;
    config.coroutine_pool.free_fn = no_op_free;
    executor = salts_coro_executor_create(&config);
    check_not_null(executor);

    check_equal(salts_coro_executor_submit(executor, &task), SALTS_OK);
    check_equal(salts_coro_executor_wait(executor), SALTS_OK);
    salts_coro_executor_get_stats(executor, &stats);

    check_equal(atomic_load_explicit(&state.runs, memory_order_acquire), 0);
    check_equal(atomic_load_explicit(&state.cancels, memory_order_acquire), 1);
    check_equal(atomic_load_explicit(&state.finalizes, memory_order_acquire), 1);
    check_equal(atomic_load_explicit(&state.cancel_status, memory_order_acquire), SALTS_ENOMEM);
    check_equal(stats.submitted_tasks, (uint64_t)1);
    check_equal(stats.started_tasks, (uint64_t)0);
    check_equal(stats.completed_tasks, (uint64_t)0);
    check_equal(stats.cancelled_tasks, (uint64_t)1);
    check_equal(stats.active_tasks, (uint64_t)0);
    check_equal(salts_coro_executor_destroy(executor), SALTS_OK);
  }

  it("rejects malformed bounded configurations") {
    salts_coro_executor_config_t config = SALTS_CORO_EXECUTOR_CONFIG_DEFAULT;

    config.queue_capacity_per_worker = 3u;
    check_null(salts_coro_executor_create(&config));

    config = (salts_coro_executor_config_t)SALTS_CORO_EXECUTOR_CONFIG_DEFAULT;
    config.coroutine_pool.max_capacity = 0u;
    check_null(salts_coro_executor_create(&config));
  }
}
