#include "salts_coro_executor.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <salts/disruptor.h>
#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/thread.h>
#include <salts/thread_pool.h>

typedef struct salts_coro_executor_shard_s salts_coro_executor_shard_t;

typedef struct salts_coro_executor_await_slot_s {
  coro_t *coroutine;
  uint32_t generation;
  int completion_status;
  uint64_t deadline_ms;
  int active;
  int waiting;
  int completion_ready;
  int timeout_enabled;
} salts_coro_executor_await_slot_t;

struct salts_coro_executor_shard_s {
  salts_coro_executor_t *executor;
  size_t index;
  disruptor_t *queue;
  disruptor_consumer_t consumer;
  uint64_t next_sequence;
  size_t queued_depth;
  disruptor_t *wake_queue;
  disruptor_consumer_t wake_consumer;
  uint64_t next_wake_sequence;
  size_t wake_depth;
  size_t wake_capacity;
  salts_coro_executor_await_slot_t *await_slots;
  uint32_t *free_await_slots;
  size_t await_capacity;
  size_t free_await_count;
  atomic_size_t active_await_count;
  salts_mutex_t mutex;
  salts_cond_t work_available;
  salts_cond_t queue_space;
  coro_scheduler_t *scheduler;
  salts_coro_pool_t *pool;
};

struct salts_coro_executor_s {
  salts_threadpool_t *workers;
  salts_coro_executor_shard_t *shards;
  salts_coro_pool_config_t pool_config;
  size_t worker_count;
  size_t queue_capacity_per_worker;

  atomic_int accepting;
  atomic_int shutdown;
  atomic_uint_fast64_t round_robin;
  atomic_uint_fast64_t submitted_tasks;
  atomic_uint_fast64_t started_tasks;
  atomic_uint_fast64_t completed_tasks;
  atomic_uint_fast64_t cancelled_tasks;
  atomic_uint_fast64_t rejected_tasks;
  atomic_uint_fast64_t queued_tasks;
  atomic_uint_fast64_t active_tasks;
  atomic_uint_fast64_t peak_queued_tasks;
  atomic_uint_fast64_t active_awaits;
  atomic_uint_fast64_t waiting_awaits;

  salts_mutex_t startup_mutex;
  salts_cond_t startup_done;
  size_t startup_count;
  int startup_failed;

  salts_mutex_t wait_mutex;
  salts_cond_t all_done;
};

static SALTS_THREAD_LOCAL salts_coro_executor_t *salts_coro_executor_tls = NULL;
static SALTS_THREAD_LOCAL size_t salts_coro_executor_shard_tls = SIZE_MAX;

static int salts_coro_executor_is_power_of_two(size_t value) {
  return value != 0u && (value & (value - 1u)) == 0u;
}

static int salts_coro_executor_next_power_of_two(size_t value, size_t *result) {
  size_t power = 1u;
  if (value == 0u || result == NULL) return 0;
  while (power < value) {
    if (power > SIZE_MAX / 2u) return 0;
    power *= 2u;
  }
  *result = power;
  return 1;
}

static uint32_t salts_coro_executor_next_generation(uint32_t generation) {
  ++generation;
  return generation == 0u ? 1u : generation;
}

static int salts_coro_executor_await_handle_well_formed(salts_coro_executor_await_t await_handle) {
  return await_handle.owner != (uintptr_t)0 && await_handle.slot != 0u &&
         await_handle.generation != 0u && await_handle.reserved == 0u;
}

static uint64_t salts_coro_executor_pending(const salts_coro_executor_t *executor) {
  uint64_t submitted = atomic_load(&executor->submitted_tasks);
  uint64_t settled =
      atomic_load(&executor->completed_tasks) + atomic_load(&executor->cancelled_tasks);
  return submitted >= settled ? submitted - settled : 0u;
}

static void salts_coro_executor_notify_progress(salts_coro_executor_t *executor) {
  salts_mutex_lock(&executor->wait_mutex);
  if (salts_coro_executor_pending(executor) == 0u) salts_cond_broadcast(&executor->all_done);
  salts_mutex_unlock(&executor->wait_mutex);
}

static void salts_coro_executor_update_peak(atomic_uint_fast64_t *peak, uint64_t candidate) {
  uint_fast64_t seen = atomic_load(peak);
  while (candidate > seen && !atomic_compare_exchange_weak(peak, &seen, (uint_fast64_t)candidate)) {
  }
}

static void salts_coro_executor_wake_shards(salts_coro_executor_t *executor) {
  if (executor == NULL || executor->shards == NULL) return;
  for (size_t index = 0u; index < executor->worker_count; ++index) {
    salts_coro_executor_shard_t *shard = &executor->shards[index];
    salts_mutex_lock(&shard->mutex);
    salts_cond_broadcast(&shard->work_available);
    salts_cond_broadcast(&shard->queue_space);
    salts_mutex_unlock(&shard->mutex);
  }
}

static void salts_coro_executor_stop_internal(salts_coro_executor_t *executor) {
  atomic_store(&executor->accepting, 0);
  atomic_store(&executor->shutdown, 1);
  salts_coro_executor_wake_shards(executor);
  salts_coro_executor_notify_progress(executor);
}

/* shard->mutex protects await slots, their free list, and wake queue depth. */
static salts_coro_executor_await_slot_t *
salts_coro_executor_find_await_locked(salts_coro_executor_shard_t *shard,
                                      salts_coro_executor_await_t await_handle) {
  salts_coro_executor_await_slot_t *slot;
  if (shard == NULL || await_handle.slot == 0u || await_handle.slot > shard->await_capacity)
    return NULL;
  slot = &shard->await_slots[await_handle.slot - 1u];
  return slot->active && slot->generation == await_handle.generation ? slot : NULL;
}

static void salts_coro_executor_release_await_locked(salts_coro_executor_shard_t *shard,
                                                     uint32_t index) {
  salts_coro_executor_await_slot_t *slot = &shard->await_slots[index];
  salts_coro_executor_t *executor = shard->executor;
  const uint32_t generation = slot->generation;
  const int was_waiting = slot->waiting;

  memset(slot, 0, sizeof(*slot));
  slot->generation = generation;
  shard->free_await_slots[shard->free_await_count++] = index;
  atomic_fetch_sub(&shard->active_await_count, 1u);
  atomic_fetch_sub(&executor->active_awaits, 1u);
  if (was_waiting) atomic_fetch_sub(&executor->waiting_awaits, 1u);
}

static void salts_coro_executor_release_coro_await(salts_coro_executor_shard_t *shard,
                                                   coro_t *coroutine) {
  if (atomic_load(&shard->active_await_count) == 0u) return;
  salts_mutex_lock(&shard->mutex);
  for (uint32_t index = 0u; index < shard->await_capacity; ++index) {
    salts_coro_executor_await_slot_t *slot = &shard->await_slots[index];
    if (slot->active && slot->coroutine == coroutine) {
      salts_coro_executor_release_await_locked(shard, index);
      break;
    }
  }
  salts_mutex_unlock(&shard->mutex);
}

static void salts_coro_executor_finish_coro(coro_t *coroutine, void *arg) {
  salts_coro_executor_shard_t *shard = (salts_coro_executor_shard_t *)arg;
  salts_coro_executor_t *executor = shard->executor;

  salts_coro_executor_release_coro_await(shard, coroutine);
  salts_coro_pool_release(shard->pool, coroutine);
  atomic_fetch_sub(&executor->active_tasks, 1u);
  atomic_fetch_add(&executor->completed_tasks, 1u);
  salts_coro_executor_notify_progress(executor);
}

static void salts_coro_executor_run_coro(coro_t *coroutine, void *arg) {
  salts_coro_executor_task_t task;
  (void)arg;

  if (coro_pop(coroutine, &task, sizeof(task)) != 0) return;
  task.run(coroutine, task.arg);
  if (task.finalize != NULL) task.finalize(task.arg);
}

static void salts_coro_executor_cancel_task(salts_coro_executor_shard_t *shard,
                                            const salts_coro_executor_task_t *task, int status) {
  salts_coro_executor_t *executor = shard->executor;
  if (task->cancel != NULL) task->cancel(task->arg, status);
  if (task->finalize != NULL) task->finalize(task->arg);
  atomic_fetch_add(&executor->cancelled_tasks, 1u);
  salts_coro_executor_notify_progress(executor);
}

static int salts_coro_executor_take_task(salts_coro_executor_shard_t *shard,
                                         salts_coro_executor_task_t *task) {
  disruptor_cursor_t available;
  disruptor_cursor_t cursor;
  const salts_coro_executor_task_t *entry;

  salts_mutex_lock(&shard->mutex);
  if (shard->queued_depth == 0u) {
    salts_mutex_unlock(&shard->mutex);
    return 0;
  }

  available.sequence = shard->next_sequence;
  /* The broadcast wait reports the latest contiguous sequence. Read and
   * release only next_sequence so per-shard FIFO entries are never skipped. */
  if (!disruptor_consumer_wait_for_nonblocking_for(shard->queue, &shard->consumer, &available)) {
    salts_mutex_unlock(&shard->mutex);
    return 0;
  }

  cursor.sequence = shard->next_sequence;
  entry = (const salts_coro_executor_task_t *)disruptor_show_entry(shard->queue, &cursor);
  if (entry == NULL || entry->run == NULL) {
    salts_mutex_unlock(&shard->mutex);
    return 0;
  }

  *task = *entry;
  disruptor_consumer_release_entry(shard->queue, &shard->consumer, &cursor);
  shard->next_sequence++;
  shard->queued_depth--;
  atomic_fetch_sub(&shard->executor->queued_tasks, 1u);
  salts_cond_broadcast(&shard->queue_space);
  salts_mutex_unlock(&shard->mutex);
  return 1;
}

static int salts_coro_executor_dispatch_wake(salts_coro_executor_shard_t *shard) {
  disruptor_cursor_t available;
  disruptor_cursor_t cursor;
  const salts_coro_executor_await_t *entry;
  salts_coro_executor_await_t await_handle;
  salts_coro_executor_await_slot_t *slot;

  salts_mutex_lock(&shard->mutex);
  if (shard->wake_depth == 0u) {
    salts_mutex_unlock(&shard->mutex);
    return 0;
  }

  available.sequence = shard->next_wake_sequence;
  if (!disruptor_consumer_wait_for_nonblocking_for(shard->wake_queue, &shard->wake_consumer,
                                                   &available)) {
    salts_mutex_unlock(&shard->mutex);
    return 0;
  }

  cursor.sequence = shard->next_wake_sequence;
  entry = (const salts_coro_executor_await_t *)disruptor_show_entry(shard->wake_queue, &cursor);
  if (entry == NULL) {
    salts_mutex_unlock(&shard->mutex);
    return 0;
  }

  await_handle = *entry;
  disruptor_consumer_release_entry(shard->wake_queue, &shard->wake_consumer, &cursor);
  shard->next_wake_sequence++;
  shard->wake_depth--;

  slot = salts_coro_executor_find_await_locked(shard, await_handle);
  if (slot != NULL && slot->waiting && slot->completion_ready)
    coro_set_waiting_for_io(slot->coroutine, 0);
  salts_mutex_unlock(&shard->mutex);
  return 1;
}

static int salts_coro_executor_dispatch_timeouts(salts_coro_executor_shard_t *shard) {
  const uint64_t now_ms = salts_monotonic_ms();
  int progressed = 0;
  if (atomic_load(&shard->active_await_count) == 0u) return 0;
  salts_mutex_lock(&shard->mutex);
  for (size_t index = 0u; index < shard->await_capacity; ++index) {
    salts_coro_executor_await_slot_t *slot = &shard->await_slots[index];
    if (!slot->active || !slot->waiting || !slot->timeout_enabled || slot->completion_ready ||
        slot->deadline_ms > now_ms)
      continue;
    slot->completion_status = SALTS_ETIMEDOUT;
    slot->completion_ready = 1;
    coro_set_waiting_for_io(slot->coroutine, 0);
    progressed = 1;
  }
  salts_mutex_unlock(&shard->mutex);
  return progressed;
}

static uint64_t salts_coro_executor_next_timeout_ns_locked(
    const salts_coro_executor_shard_t *shard) {
  const uint64_t now_ms = salts_monotonic_ms();
  uint64_t earliest_ms = UINT64_MAX;
  for (size_t index = 0u; index < shard->await_capacity; ++index) {
    const salts_coro_executor_await_slot_t *slot = &shard->await_slots[index];
    if (slot->active && slot->waiting && slot->timeout_enabled && !slot->completion_ready &&
        slot->deadline_ms < earliest_ms)
      earliest_ms = slot->deadline_ms;
  }
  if (earliest_ms == UINT64_MAX) return UINT64_MAX;
  if (earliest_ms <= now_ms) return 1u;
  return (earliest_ms - now_ms) * UINT64_C(1000000);
}

static void salts_coro_executor_start_task(salts_coro_executor_shard_t *shard,
                                           const salts_coro_executor_task_t *task) {
  salts_coro_executor_t *executor = shard->executor;
  coro_t *coroutine = salts_coro_pool_acquire(shard->pool, salts_coro_executor_run_coro, shard);

  if (coroutine == NULL) {
    salts_coro_executor_cancel_task(shard, task, SALTS_ENOMEM);
    return;
  }
  coro_set_data(coroutine, NULL);
  if (coro_push(coroutine, task, sizeof(*task)) != 0) {
    (void)salts_coro_pool_abandon(shard->pool, coroutine);
    salts_coro_executor_cancel_task(shard, task, SALTS_ENOBUFS);
    return;
  }

  coro_set_cleanup(coroutine, salts_coro_executor_finish_coro, shard);
  coro_scheduler_adopt(shard->scheduler, coroutine);
  atomic_fetch_add(&executor->started_tasks, 1u);
  atomic_fetch_add(&executor->active_tasks, 1u);
}

static int salts_coro_executor_shard_should_exit(salts_coro_executor_shard_t *shard) {
  return atomic_load(&shard->executor->shutdown) && shard->queued_depth == 0u &&
         shard->wake_depth == 0u && salts_coro_pool_active_count(shard->pool) == 0u;
}

static void salts_coro_executor_report_startup(salts_coro_executor_shard_t *shard, int failed) {
  salts_coro_executor_t *executor = shard->executor;
  salts_mutex_lock(&executor->startup_mutex);
  if (failed) executor->startup_failed = 1;
  executor->startup_count++;
  salts_cond_broadcast(&executor->startup_done);
  salts_mutex_unlock(&executor->startup_mutex);

  if (failed) salts_coro_executor_stop_internal(executor);
}

static void salts_coro_executor_worker(void *arg) {
  salts_coro_executor_shard_t *shard = (salts_coro_executor_shard_t *)arg;
  salts_coro_executor_t *executor = shard->executor;
  salts_coro_executor_t *previous_executor = salts_coro_executor_tls;
  size_t previous_shard = salts_coro_executor_shard_tls;
  int startup_failed = 0;
  int task_consumer_registered = 0;
  int wake_consumer_registered = 0;

  /* One persistent backing-pool task owns this scheduler, pool, and consumer
   * until drain completes. No live frame crosses this thread boundary. */
  salts_coro_executor_tls = executor;
  salts_coro_executor_shard_tls = shard->index;
  shard->scheduler = coro_scheduler_create();
  shard->pool = salts_coro_pool_create(&executor->pool_config);
  if (shard->scheduler == NULL || shard->pool == NULL) {
    startup_failed = 1;
  } else if (disruptor_consumer_try_register(shard->queue, &shard->consumer,
                                             &shard->next_sequence)) {
    task_consumer_registered = 1;
  } else {
    startup_failed = 1;
  }
  if (!startup_failed && disruptor_consumer_try_register(shard->wake_queue, &shard->wake_consumer,
                                                         &shard->next_wake_sequence)) {
    wake_consumer_registered = 1;
  } else if (!startup_failed) {
    startup_failed = 1;
  }

  salts_coro_executor_report_startup(shard, startup_failed);

  if (!startup_failed) {
    for (;;) {
      int progressed = 0;

      if (salts_coro_executor_dispatch_timeouts(shard)) progressed = 1;
      while (salts_coro_executor_dispatch_wake(shard))
        progressed = 1;

      while (salts_coro_pool_active_count(shard->pool) < executor->pool_config.max_capacity) {
        salts_coro_executor_task_t task;
        if (!salts_coro_executor_take_task(shard, &task)) break;
        salts_coro_executor_start_task(shard, &task);
        progressed = 1;
      }

      if (coro_scheduler_has_ready(shard->scheduler)) {
        (void)coro_scheduler_tick(shard->scheduler);
        progressed = 1;
      }

      salts_mutex_lock(&shard->mutex);
      if (salts_coro_executor_shard_should_exit(shard)) {
        salts_mutex_unlock(&shard->mutex);
        break;
      }
      if (!progressed) {
        if (!salts_coro_executor_shard_should_exit(shard) && shard->wake_depth == 0u &&
            !(shard->queued_depth > 0u &&
              salts_coro_pool_active_count(shard->pool) < executor->pool_config.max_capacity) &&
            !coro_scheduler_has_ready(shard->scheduler)) {
          const uint64_t timeout_ns = salts_coro_executor_next_timeout_ns_locked(shard);
          if (timeout_ns == UINT64_MAX)
            salts_cond_wait(&shard->work_available, &shard->mutex);
          else
            (void)salts_cond_timedwait(&shard->work_available, &shard->mutex, timeout_ns);
        }
      }
      salts_mutex_unlock(&shard->mutex);
    }
  }

  if (wake_consumer_registered)
    disruptor_consumer_unregister(shard->wake_queue, &shard->wake_consumer);
  if (task_consumer_registered) disruptor_consumer_unregister(shard->queue, &shard->consumer);

  coro_scheduler_destroy(shard->scheduler);
  salts_coro_pool_destroy(shard->pool);
  shard->scheduler = NULL;
  shard->pool = NULL;
  salts_coro_executor_tls = previous_executor;
  salts_coro_executor_shard_tls = previous_shard;
}

static int salts_coro_executor_config_valid(const salts_coro_executor_config_t *config,
                                            size_t *worker_count) {
  int detected_workers;

  if (config == NULL || worker_count == NULL) return 0;
  *worker_count = config->worker_count;
  if (*worker_count == 0u) {
    detected_workers = salts_cpu_count();
    if (detected_workers <= 0) return 0;
    *worker_count = (size_t)detected_workers;
  }

  if (*worker_count == 0u || *worker_count > (size_t)INT_MAX ||
      *worker_count > SIZE_MAX / sizeof(salts_coro_executor_shard_t) ||
      !salts_coro_executor_is_power_of_two(config->queue_capacity_per_worker) ||
      config->queue_capacity_per_worker > (size_t)INT64_MAX ||
      config->coroutine_pool.max_capacity == 0u ||
      config->coroutine_pool.max_capacity > (size_t)INT_MAX ||
      config->coroutine_pool.initial_capacity > config->coroutine_pool.max_capacity ||
      (config->coroutine_pool.storage_size != 0u &&
       config->coroutine_pool.storage_size < sizeof(salts_coro_executor_task_t)) ||
      ((config->coroutine_pool.alloc_fn == NULL) != (config->coroutine_pool.free_fn == NULL)))
    return 0;

  return 1;
}

static void salts_coro_executor_destroy_storage(salts_coro_executor_t *executor) {
  if (executor == NULL) return;
  if (executor->shards != NULL) {
    for (size_t index = 0u; index < executor->worker_count; ++index) {
      salts_coro_executor_shard_t *shard = &executor->shards[index];
      disruptor_destroy(shard->queue);
      disruptor_destroy(shard->wake_queue);
      free(shard->free_await_slots);
      free(shard->await_slots);
      salts_mutex_destroy(&shard->mutex);
      salts_cond_destroy(&shard->work_available);
      salts_cond_destroy(&shard->queue_space);
    }
  }
  free(executor->shards);
  salts_mutex_destroy(&executor->startup_mutex);
  salts_cond_destroy(&executor->startup_done);
  salts_mutex_destroy(&executor->wait_mutex);
  salts_cond_destroy(&executor->all_done);
  free(executor);
}

salts_coro_executor_t *salts_coro_executor_create(const salts_coro_executor_config_t *config) {
  salts_coro_executor_config_t defaults = SALTS_CORO_EXECUTOR_CONFIG_DEFAULT;
  salts_coro_executor_t *executor;
  salts_threadpool_config_t worker_config;
  size_t worker_count;

  if (config == NULL) config = &defaults;
  if (!salts_coro_executor_config_valid(config, &worker_count)) return NULL;

  executor = (salts_coro_executor_t *)calloc(1, sizeof(*executor));
  if (executor == NULL) return NULL;
  executor->worker_count = worker_count;
  executor->queue_capacity_per_worker = config->queue_capacity_per_worker;
  executor->pool_config = config->coroutine_pool;
  atomic_store(&executor->accepting, 1);

  salts_mutex_init(&executor->startup_mutex);
  salts_cond_init(&executor->startup_done);
  salts_mutex_init(&executor->wait_mutex);
  salts_cond_init(&executor->all_done);
  if (executor->startup_mutex == NULL || executor->startup_done == NULL ||
      executor->wait_mutex == NULL || executor->all_done == NULL) {
    salts_coro_executor_destroy_storage(executor);
    return NULL;
  }

  executor->shards = (salts_coro_executor_shard_t *)calloc(worker_count, sizeof(*executor->shards));
  if (executor->shards == NULL) {
    salts_coro_executor_destroy_storage(executor);
    return NULL;
  }

  for (size_t index = 0u; index < worker_count; ++index) {
    salts_coro_executor_shard_t *shard = &executor->shards[index];
    size_t wake_capacity;
    disruptor_config_t queue_config = {
        sizeof(salts_coro_executor_task_t),
        (uint64_t)executor->queue_capacity_per_worker,
        1u,
        DISRUPTOR_MODE_BROADCAST,
    };
    disruptor_config_t wake_queue_config;
    if (!salts_coro_executor_next_power_of_two(executor->pool_config.max_capacity,
                                               &wake_capacity)) {
      salts_coro_executor_destroy_storage(executor);
      return NULL;
    }
    wake_queue_config = (disruptor_config_t){sizeof(salts_coro_executor_await_t),
                                             (uint64_t)wake_capacity, 1u, DISRUPTOR_MODE_BROADCAST};
    shard->executor = executor;
    shard->index = index;
    shard->await_capacity = executor->pool_config.max_capacity;
    shard->free_await_count = shard->await_capacity;
    shard->wake_capacity = wake_capacity;
    salts_mutex_init(&shard->mutex);
    salts_cond_init(&shard->work_available);
    salts_cond_init(&shard->queue_space);
    shard->queue = disruptor_create(&queue_config);
    shard->wake_queue = disruptor_create(&wake_queue_config);
    shard->await_slots = (salts_coro_executor_await_slot_t *)calloc(shard->await_capacity,
                                                                    sizeof(*shard->await_slots));
    shard->free_await_slots =
        (uint32_t *)calloc(shard->await_capacity, sizeof(*shard->free_await_slots));
    if (shard->mutex == NULL || shard->work_available == NULL || shard->queue_space == NULL ||
        shard->queue == NULL || shard->wake_queue == NULL || shard->await_slots == NULL ||
        shard->free_await_slots == NULL) {
      salts_coro_executor_destroy_storage(executor);
      return NULL;
    }
    for (size_t slot = 0u; slot < shard->await_capacity; ++slot)
      shard->free_await_slots[slot] = (uint32_t)(shard->await_capacity - slot - 1u);
  }

  worker_config.num_threads = (int)worker_count;
  worker_config.queue_capacity = worker_count;
  executor->workers = salts_threadpool_create_with_config(&worker_config);
  if (executor->workers == NULL) {
    salts_coro_executor_destroy_storage(executor);
    return NULL;
  }

  for (size_t index = 0u; index < worker_count; ++index) {
    if (salts_threadpool_submit(executor->workers, salts_coro_executor_worker,
                                &executor->shards[index]) != SALTS_OK) {
      salts_coro_executor_stop_internal(executor);
      salts_threadpool_shutdown(executor->workers);
      salts_threadpool_destroy(executor->workers);
      executor->workers = NULL;
      salts_coro_executor_destroy_storage(executor);
      return NULL;
    }
  }

  salts_mutex_lock(&executor->startup_mutex);
  while (executor->startup_count < executor->worker_count)
    salts_cond_wait(&executor->startup_done, &executor->startup_mutex);
  salts_mutex_unlock(&executor->startup_mutex);

  if (executor->startup_failed) {
    salts_threadpool_shutdown(executor->workers);
    salts_threadpool_destroy(executor->workers);
    executor->workers = NULL;
    salts_coro_executor_destroy_storage(executor);
    return NULL;
  }

  return executor;
}

static int salts_coro_executor_submit_internal(salts_coro_executor_t *executor, size_t shard_index,
                                               const salts_coro_executor_task_t *task,
                                               int blocking) {
  salts_coro_executor_shard_t *shard;
  disruptor_cursor_t cursor = {0};
  salts_coro_executor_task_t *entry;
  uint64_t queued;

  if (executor == NULL) return SALTS_EINVAL;
  if (task == NULL || task->run == NULL || shard_index >= executor->worker_count) {
    atomic_fetch_add(&executor->rejected_tasks, 1u);
    return SALTS_EINVAL;
  }

  shard = &executor->shards[shard_index];
  salts_mutex_lock(&shard->mutex);
  while (atomic_load(&executor->accepting) &&
         shard->queued_depth >= executor->queue_capacity_per_worker) {
    if (!blocking || salts_coro_executor_tls == executor) {
      salts_mutex_unlock(&shard->mutex);
      atomic_fetch_add(&executor->rejected_tasks, 1u);
      return blocking && salts_coro_executor_tls == executor ? SALTS_EBUSY : SALTS_ENOBUFS;
    }
    salts_cond_wait(&shard->queue_space, &shard->mutex);
  }

  if (!atomic_load(&executor->accepting)) {
    salts_mutex_unlock(&shard->mutex);
    atomic_fetch_add(&executor->rejected_tasks, 1u);
    return SALTS_ESHUTDOWN;
  }
  if (!disruptor_publisher_try_claim(shard->queue, &cursor)) {
    salts_mutex_unlock(&shard->mutex);
    atomic_fetch_add(&executor->rejected_tasks, 1u);
    return SALTS_ENOBUFS;
  }

  entry = (salts_coro_executor_task_t *)disruptor_acquire_entry(shard->queue, &cursor);
  *entry = *task;
  disruptor_publisher_commit_entry_blocking(shard->queue, &cursor);
  shard->queued_depth++;
  queued = atomic_fetch_add(&executor->queued_tasks, 1u) + 1u;
  atomic_fetch_add(&executor->submitted_tasks, 1u);
  salts_coro_executor_update_peak(&executor->peak_queued_tasks, queued);
  salts_cond_signal(&shard->work_available);
  salts_mutex_unlock(&shard->mutex);
  return SALTS_OK;
}

int salts_coro_executor_submit_to(salts_coro_executor_t *executor, size_t shard,
                                  const salts_coro_executor_task_t *task) {
  return salts_coro_executor_submit_internal(executor, shard, task, 1);
}

int salts_coro_executor_try_submit_to(salts_coro_executor_t *executor, size_t shard,
                                      const salts_coro_executor_task_t *task) {
  return salts_coro_executor_submit_internal(executor, shard, task, 0);
}

int salts_coro_executor_submit(salts_coro_executor_t *executor,
                               const salts_coro_executor_task_t *task) {
  size_t shard;
  if (executor == NULL) return SALTS_EINVAL;
  shard = (size_t)(atomic_fetch_add(&executor->round_robin, 1u) % executor->worker_count);
  return salts_coro_executor_submit_internal(executor, shard, task, 1);
}

int salts_coro_executor_try_submit(salts_coro_executor_t *executor,
                                   const salts_coro_executor_task_t *task) {
  size_t shard;
  if (executor == NULL) return SALTS_EINVAL;
  shard = (size_t)(atomic_fetch_add(&executor->round_robin, 1u) % executor->worker_count);
  return salts_coro_executor_submit_internal(executor, shard, task, 0);
}

static salts_coro_executor_shard_t *salts_coro_executor_current_context(coro_t **coroutine) {
  salts_coro_executor_shard_t *shard;
  coro_t *running;
  if (coroutine != NULL) *coroutine = NULL;
  if (salts_coro_executor_tls == NULL ||
      salts_coro_executor_shard_tls >= salts_coro_executor_tls->worker_count)
    return NULL;
  shard = &salts_coro_executor_tls->shards[salts_coro_executor_shard_tls];
  running = coro_running();
  if (running == NULL || !coro_is_scheduled(running) ||
      coro_current_scheduler() != shard->scheduler)
    return NULL;
  if (coroutine != NULL) *coroutine = running;
  return shard;
}

int salts_coro_executor_yield(void) {
  if (salts_coro_executor_current_context(NULL) == NULL) return SALTS_EINVAL;
  return coro_yield() == 0 ? SALTS_OK : SALTS_EIO;
}

int salts_coro_executor_await_begin(salts_coro_executor_await_t *out_await) {
  salts_coro_executor_shard_t *shard;
  salts_coro_executor_await_slot_t *slot;
  coro_t *coroutine;
  uint32_t index;
  uint32_t generation;

  if (out_await != NULL) *out_await = (salts_coro_executor_await_t){0};
  shard = salts_coro_executor_current_context(&coroutine);
  if (shard == NULL || out_await == NULL) return SALTS_EINVAL;

  salts_mutex_lock(&shard->mutex);
  for (size_t slot_index = 0u; slot_index < shard->await_capacity; ++slot_index) {
    slot = &shard->await_slots[slot_index];
    if (slot->active && slot->coroutine == coroutine) {
      salts_mutex_unlock(&shard->mutex);
      return SALTS_EBUSY;
    }
  }
  if (shard->free_await_count == 0u) {
    salts_mutex_unlock(&shard->mutex);
    return SALTS_ENOBUFS;
  }

  index = shard->free_await_slots[--shard->free_await_count];
  slot = &shard->await_slots[index];
  generation = salts_coro_executor_next_generation(slot->generation);
  memset(slot, 0, sizeof(*slot));
  slot->coroutine = coroutine;
  slot->generation = generation;
  slot->active = 1;
  *out_await = (salts_coro_executor_await_t){(uintptr_t)shard->executor, (uint32_t)shard->index,
                                             index + 1u, generation, 0u};
  atomic_fetch_add(&shard->active_await_count, 1u);
  atomic_fetch_add(&shard->executor->active_awaits, 1u);
  salts_mutex_unlock(&shard->mutex);
  return SALTS_OK;
}

int salts_coro_executor_await_complete(salts_coro_executor_t *executor,
                                       salts_coro_executor_await_t await_handle, int status) {
  salts_coro_executor_shard_t *shard;
  salts_coro_executor_await_slot_t *slot;
  disruptor_cursor_t cursor = {0};
  salts_coro_executor_await_t *entry;

  if (executor == NULL || !salts_coro_executor_await_handle_well_formed(await_handle) ||
      await_handle.owner != (uintptr_t)executor || await_handle.shard >= executor->worker_count)
    return SALTS_EINVAL;
  shard = &executor->shards[await_handle.shard];

  salts_mutex_lock(&shard->mutex);
  slot = salts_coro_executor_find_await_locked(shard, await_handle);
  if (slot == NULL) {
    salts_mutex_unlock(&shard->mutex);
    return SALTS_ENOENT;
  }
  if (slot->completion_ready) {
    salts_mutex_unlock(&shard->mutex);
    return SALTS_EALREADY;
  }

  if (slot->waiting) {
    if (shard->wake_depth >= shard->wake_capacity ||
        !disruptor_publisher_try_claim(shard->wake_queue, &cursor)) {
      salts_mutex_unlock(&shard->mutex);
      return SALTS_ENOBUFS;
    }
    entry = (salts_coro_executor_await_t *)disruptor_acquire_entry(shard->wake_queue, &cursor);
    *entry = await_handle;
    disruptor_publisher_commit_entry_blocking(shard->wake_queue, &cursor);
    shard->wake_depth++;
  }
  slot->completion_status = status;
  slot->completion_ready = 1;
  if (slot->waiting) salts_cond_signal(&shard->work_available);
  salts_mutex_unlock(&shard->mutex);
  return SALTS_OK;
}

static int salts_coro_executor_await_internal(salts_coro_executor_await_t await_handle,
                                              uint32_t timeout_ms, int timeout_enabled,
                                              int *out_status) {
  salts_coro_executor_shard_t *shard;
  salts_coro_executor_await_slot_t *slot;
  coro_t *coroutine;
  uint32_t index;
  int status;

  if (out_status != NULL) *out_status = 0;
  shard = salts_coro_executor_current_context(&coroutine);
  if (shard == NULL || out_status == NULL ||
      !salts_coro_executor_await_handle_well_formed(await_handle) ||
      await_handle.owner != (uintptr_t)shard->executor || await_handle.shard != shard->index)
    return SALTS_EINVAL;
  index = await_handle.slot - 1u;

  salts_mutex_lock(&shard->mutex);
  slot = salts_coro_executor_find_await_locked(shard, await_handle);
  if (slot == NULL) {
    salts_mutex_unlock(&shard->mutex);
    return SALTS_ENOENT;
  }
  if (slot->coroutine != coroutine) {
    salts_mutex_unlock(&shard->mutex);
    return SALTS_EINVAL;
  }
  if (slot->completion_ready) {
    status = slot->completion_status;
    salts_coro_executor_release_await_locked(shard, index);
    salts_mutex_unlock(&shard->mutex);
    *out_status = status;
    return SALTS_OK;
  }
  if (slot->waiting) {
    salts_mutex_unlock(&shard->mutex);
    return SALTS_EALREADY;
  }

  if (timeout_enabled) {
    const uint64_t now_ms = salts_monotonic_ms();
    slot->deadline_ms =
        now_ms > UINT64_MAX - (uint64_t)timeout_ms ? UINT64_MAX : now_ms + timeout_ms;
    slot->timeout_enabled = 1;
  }
  slot->waiting = 1;
  atomic_fetch_add(&shard->executor->waiting_awaits, 1u);
  coro_set_waiting_for_io(coroutine, 1);
  salts_mutex_unlock(&shard->mutex);

  if (coro_yield() != 0) {
    salts_mutex_lock(&shard->mutex);
    slot = salts_coro_executor_find_await_locked(shard, await_handle);
    if (slot != NULL) {
      coro_set_waiting_for_io(coroutine, 0);
      salts_coro_executor_release_await_locked(shard, index);
    }
    salts_mutex_unlock(&shard->mutex);
    return SALTS_EIO;
  }

  salts_mutex_lock(&shard->mutex);
  slot = salts_coro_executor_find_await_locked(shard, await_handle);
  if (slot == NULL || slot->coroutine != coroutine || !slot->completion_ready) {
    if (slot != NULL) salts_coro_executor_release_await_locked(shard, index);
    salts_mutex_unlock(&shard->mutex);
    return SALTS_EPROTO;
  }
  status = slot->completion_status;
  salts_coro_executor_release_await_locked(shard, index);
  salts_mutex_unlock(&shard->mutex);
  *out_status = status;
  return SALTS_OK;
}

int salts_coro_executor_await(salts_coro_executor_await_t await_handle, int *out_status) {
  return salts_coro_executor_await_internal(await_handle, 0u, 0, out_status);
}

int salts_coro_executor_await_for(salts_coro_executor_await_t await_handle, uint32_t timeout_ms,
                                  int *out_status) {
  if (timeout_ms == 0u) {
    if (out_status != NULL) *out_status = 0;
    return SALTS_EINVAL;
  }
  return salts_coro_executor_await_internal(await_handle, timeout_ms, 1, out_status);
}

int salts_coro_executor_await_abort(salts_coro_executor_await_t await_handle) {
  salts_coro_executor_shard_t *shard;
  salts_coro_executor_await_slot_t *slot;
  coro_t *coroutine;
  uint32_t index;

  shard = salts_coro_executor_current_context(&coroutine);
  if (shard == NULL || !salts_coro_executor_await_handle_well_formed(await_handle) ||
      await_handle.owner != (uintptr_t)shard->executor || await_handle.shard != shard->index)
    return SALTS_EINVAL;
  index = await_handle.slot - 1u;

  salts_mutex_lock(&shard->mutex);
  slot = salts_coro_executor_find_await_locked(shard, await_handle);
  if (slot == NULL) {
    salts_mutex_unlock(&shard->mutex);
    return SALTS_ENOENT;
  }
  if (slot->coroutine != coroutine) {
    salts_mutex_unlock(&shard->mutex);
    return SALTS_EINVAL;
  }
  if (slot->completion_ready) {
    salts_mutex_unlock(&shard->mutex);
    return SALTS_EALREADY;
  }
  if (slot->waiting) {
    salts_mutex_unlock(&shard->mutex);
    return SALTS_EBUSY;
  }
  salts_coro_executor_release_await_locked(shard, index);
  salts_mutex_unlock(&shard->mutex);
  return SALTS_OK;
}

int salts_coro_executor_shutdown(salts_coro_executor_t *executor) {
  if (executor == NULL) return SALTS_EINVAL;
  salts_coro_executor_stop_internal(executor);
  return SALTS_OK;
}

int salts_coro_executor_wait(salts_coro_executor_t *executor) {
  if (executor == NULL) return SALTS_EINVAL;
  if (salts_coro_executor_tls == executor) return SALTS_EBUSY;

  salts_mutex_lock(&executor->wait_mutex);
  while (salts_coro_executor_pending(executor) > 0u)
    salts_cond_wait(&executor->all_done, &executor->wait_mutex);
  salts_mutex_unlock(&executor->wait_mutex);
  return SALTS_OK;
}

int salts_coro_executor_destroy(salts_coro_executor_t *executor) {
  int status;
  if (executor == NULL) return SALTS_EINVAL;
  if (salts_coro_executor_tls == executor) return SALTS_EBUSY;

  salts_coro_executor_stop_internal(executor);
  status = salts_coro_executor_wait(executor);
  if (status != SALTS_OK) return status;

  salts_threadpool_shutdown(executor->workers);
  status = salts_threadpool_wait_status(executor->workers);
  if (status != SALTS_OK) return status;
  salts_threadpool_destroy(executor->workers);
  executor->workers = NULL;
  salts_coro_executor_destroy_storage(executor);
  return SALTS_OK;
}

salts_coro_executor_t *salts_coro_executor_current(void) { return salts_coro_executor_tls; }

size_t salts_coro_executor_current_shard(const salts_coro_executor_t *executor) {
  return executor != NULL && salts_coro_executor_tls == executor ? salts_coro_executor_shard_tls
                                                                 : SIZE_MAX;
}

void salts_coro_executor_get_stats(const salts_coro_executor_t *executor,
                                   salts_coro_executor_stats_t *stats) {
  if (executor == NULL || stats == NULL) return;
  memset(stats, 0, sizeof(*stats));
  stats->worker_count = executor->worker_count;
  stats->queue_capacity_per_worker = executor->queue_capacity_per_worker;
  stats->accepting = atomic_load(&executor->accepting);
  stats->submitted_tasks = atomic_load(&executor->submitted_tasks);
  stats->started_tasks = atomic_load(&executor->started_tasks);
  stats->completed_tasks = atomic_load(&executor->completed_tasks);
  stats->cancelled_tasks = atomic_load(&executor->cancelled_tasks);
  stats->rejected_tasks = atomic_load(&executor->rejected_tasks);
  stats->queued_tasks = atomic_load(&executor->queued_tasks);
  stats->active_tasks = atomic_load(&executor->active_tasks);
  stats->peak_queued_tasks = atomic_load(&executor->peak_queued_tasks);
  stats->active_awaits = atomic_load(&executor->active_awaits);
  stats->waiting_awaits = atomic_load(&executor->waiting_awaits);
}
