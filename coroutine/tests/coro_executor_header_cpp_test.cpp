#include <coro_executor.h>
#include <cmeta/coroutine.h>
#include <tinytest.hpp>

suite("Coroutine C++ headers") {
  group("default configuration") {
    it("preserves the default queue and coroutine pool capacities") {
      const coro_executor_config_t config = CORO_EXECUTOR_CONFIG_DEFAULT;

      check_equal(config.queue_capacity_per_worker,
                  CORO_EXECUTOR_DEFAULT_QUEUE_CAPACITY_PER_WORKER);
      check_equal(config.coroutine_pool.max_capacity,
                  CORO_EXECUTOR_DEFAULT_MAX_COROUTINES_PER_WORKER);
    }
  }

  group("value initialization") {
    it("clears the await handle owner and slot") {
      const coro_executor_await_t await_handle{};

      check_equal(await_handle.owner, 0u);
      check_equal(await_handle.slot, 0u);
    }

    it("initializes the task callback to null") {
      const coro_executor_task_t task{};

      check_equal(task.run, nullptr);
    }

    it("initializes the worker count to zero") {
      const coro_executor_stats_t stats{};

      check_equal(stats.worker_count, 0u);
    }
  }

  group("CMeta facade outside a coroutine") {
    it("reports no current executor or shard") {
      cmeta_executor *executor = cmeta_current_executor();

      check_equal(executor, nullptr);
      check_equal(cmeta_current_shard(executor), SIZE_MAX);
    }

    it("rejects yielding") {
      check_equal(cmeta_yield(), SALTS_EINVAL);
    }

    it("rejects reserving a wait handle and leaves no owner") {
      cmeta_wait_handle wait{};

      check_equal(cmeta_wait_begin(&wait), SALTS_EINVAL);
      check_equal(wait.owner, 0u);
    }

    it("rejects waiting and clears the result status") {
      const cmeta_wait_handle wait{};
      int status = SALTS_EIO;

      check_equal(cmeta_wait(wait, &status), SALTS_EINVAL);
      check_equal(status, 0);
    }

    it("rejects timed waiting and clears the result status") {
      constexpr uint32_t timeout_ms = 1u;
      const cmeta_wait_handle wait{};
      int status = SALTS_EIO;

      check_equal(cmeta_wait_for(wait, timeout_ms, &status), SALTS_EINVAL);
      check_equal(status, 0);
    }

    it("rejects aborting an invalid wait handle") {
      const cmeta_wait_handle wait{};

      check_equal(cmeta_wait_abort(wait), SALTS_EINVAL);
    }

    it("rejects completing a wait without an executor") {
      const cmeta_wait_handle wait{};
      cmeta_executor *executor = cmeta_current_executor();

      check_equal(cmeta_wait_complete(executor, wait, SALTS_OK), SALTS_EINVAL);
    }
  }
}
