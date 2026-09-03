#include <salts_coro_executor.h>

int main() {
  salts_coro_executor_config_t config = SALTS_CORO_EXECUTOR_CONFIG_DEFAULT;
  salts_coro_executor_await_t await_handle{};
  salts_coro_executor_task_t task{};
  salts_coro_executor_stats_t stats{};

  return config.queue_capacity_per_worker ==
                     SALTS_CORO_EXECUTOR_DEFAULT_QUEUE_CAPACITY_PER_WORKER &&
                 config.coroutine_pool.max_capacity ==
                     SALTS_CORO_EXECUTOR_DEFAULT_MAX_COROUTINES_PER_WORKER &&
                 await_handle.owner == 0u && await_handle.slot == 0u && task.run == nullptr &&
                 stats.worker_count == 0u
             ? 0
             : 1;
}
