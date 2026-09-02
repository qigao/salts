#include "tinytest.h"

#include <turbo/thread.h>
#include <turbo_coro_executor.h>

#include <stdatomic.h>
#include <stdint.h>

enum {
  CORO_EXECUTOR_BENCH_SAMPLES = 10,
  CORO_EXECUTOR_TASKS_PER_SAMPLE = 2048,
  CORO_EXECUTOR_WORK_ROUNDS = 2048,
  CORO_EXECUTOR_MAX_BENCH_WORKERS = 4,
  CORO_EXECUTOR_BENCH_QUEUE_CAPACITY = 2048,
  CORO_EXECUTOR_BENCH_ACTIVE_PER_WORKER = 64
};

static atomic_uint_fast64_t coro_executor_bench_sink;
static atomic_uint_fast64_t coro_executor_bench_sequence;

static void cpu_yield_task(coro_t *coroutine, void *arg) {
  uint64_t value =
      atomic_fetch_add_explicit(&coro_executor_bench_sequence, 1u, memory_order_relaxed) + 1u;
  (void)coroutine;
  (void)arg;

  for (int round = 0; round < CORO_EXECUTOR_WORK_ROUNDS / 2; ++round)
    value = value * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
  (void)coro_yield();
  for (int round = CORO_EXECUTOR_WORK_ROUNDS / 2; round < CORO_EXECUTOR_WORK_ROUNDS; ++round)
    value = value * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);

  atomic_fetch_add_explicit(&coro_executor_bench_sink, value, memory_order_relaxed);
}

static turbo_coro_executor_t *create_bench_executor(size_t workers) {
  turbo_coro_executor_config_t config = TURBO_CORO_EXECUTOR_CONFIG_DEFAULT;
  config.worker_count = workers;
  config.queue_capacity_per_worker = CORO_EXECUTOR_BENCH_QUEUE_CAPACITY;
  config.coroutine_pool.initial_capacity = 0u;
  config.coroutine_pool.max_capacity = CORO_EXECUTOR_BENCH_ACTIVE_PER_WORKER;
  return turbo_coro_executor_create(&config);
}

static int run_bench_batch(turbo_coro_executor_t *executor,
                           const turbo_coro_executor_task_t *task) {
  for (int index = 0; index < CORO_EXECUTOR_TASKS_PER_SAMPLE; ++index) {
    if (turbo_coro_executor_submit(executor, task) != TURBO_OK) return 0;
  }
  return turbo_coro_executor_wait(executor) == TURBO_OK;
}

suite("Coroutine Executor Release benchmarks") {
  bench("compares one shard with bounded multi-shard execution") {
    turbo_coro_executor_t *single = create_bench_executor(1u);
    turbo_coro_executor_t *sharded;
    turbo_coro_executor_task_t task = {cpu_yield_task, NULL, NULL, NULL};
    turbo_coro_executor_stats_t single_stats = {0};
    turbo_coro_executor_stats_t sharded_stats = {0};
    int cpu_count = turbo_cpu_count();
    size_t worker_count = cpu_count > CORO_EXECUTOR_MAX_BENCH_WORKERS
                              ? CORO_EXECUTOR_MAX_BENCH_WORKERS
                              : (size_t)(cpu_count > 0 ? cpu_count : 1);
    int single_ok = 1;
    int sharded_ok = 1;
    const uint64_t expected =
        (uint64_t)CORO_EXECUTOR_BENCH_SAMPLES * CORO_EXECUTOR_TASKS_PER_SAMPLE;

    sharded = create_bench_executor(worker_count);
    check_not_null(single);
    check_not_null(sharded);

    benchmark_ops("one shard / CPU work + one yield", CORO_EXECUTOR_BENCH_SAMPLES,
                  CORO_EXECUTOR_TASKS_PER_SAMPLE) {
      if (!run_bench_batch(single, &task)) single_ok = 0;
    }
    benchmark_ops("up to four shards / CPU work + one yield", CORO_EXECUTOR_BENCH_SAMPLES,
                  CORO_EXECUTOR_TASKS_PER_SAMPLE) {
      if (!run_bench_batch(sharded, &task)) sharded_ok = 0;
    }

    turbo_coro_executor_get_stats(single, &single_stats);
    turbo_coro_executor_get_stats(sharded, &sharded_stats);
    check_true(single_ok);
    check_true(sharded_ok);
    check_equal(single_stats.completed_tasks, expected);
    check_equal(sharded_stats.completed_tasks, expected);
    check_equal(turbo_coro_executor_destroy(single), TURBO_OK);
    check_equal(turbo_coro_executor_destroy(sharded), TURBO_OK);
  }
}
