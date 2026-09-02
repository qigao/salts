#include "turbo_coro_executor.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <turbo/disruptor.h>
#include <turbo/error_codes.h>
#include <turbo/thread.h>
#include <turbo/thread_pool.h>

typedef struct turbo_coro_executor_shard_s turbo_coro_executor_shard_t;

typedef struct turbo_coro_executor_await_slot_s {
  coro_t *coroutine;
  uint32_t generation;
  int completion_status;
  int active;
  int waiting;
  int completion_ready;
} turbo_coro_executor_await_slot_t;

struct turbo_coro_executor_shard_s {
  turbo_coro_executor_t *executor;
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
  turbo_coro_executor_await_slot_t *await_slots;
  uint32_t *free_await_slots;
  size_t await_capacity;
  size_t free_await_count;
  atomic_size_t active_await_count;
  turbo_mutex_t mutex;
  turbo_cond_t work_available;
  turbo_cond_t queue_space;
  coro_scheduler_t *scheduler;
  turbo_coro_pool_t *pool;
};

struct turbo_coro_executor_s {
  turbo_threadpool_t *workers;
  turbo_coro_executor_shard_t *shards;
  turbo_coro_pool_config_t pool_config;
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

  turbo_mutex_t startup_mutex;
  turbo_cond_t startup_done;
  size_t startup_count;
  int startup_failed;

  turbo_mutex_t wait_mutex;
  turbo_cond_t all_done;
};

static TURBO_THREAD_LOCAL turbo_coro_executor_t *turbo_coro_executor_tls = NULL;
static TURBO_THREAD_LOCAL size_t turbo_coro_executor_shard_tls = SIZE_MAX;

static int turbo_coro_executor_is_power_of_two(size_t value) {
  return value != 0u && (value & (value - 1u)) == 0u;
}

static int turbo_coro_executor_next_power_of_two(size_t value, size_t *result) {
  size_t power = 1u;
  if (value == 0u || result == NULL) return 0;
  while (power < value) {
    if (power > SIZE_MAX / 2u) return 0;
    power *= 2u;
  }
  *result = power;
  return 1;
}

static uint32_t turbo_coro_executor_next_generation(uint32_t generation) {
  ++generation;
  return generation == 0u ? 1u : generation;
}

static int turbo_coro_executor_await_handle_well_formed(turbo_coro_executor_await_t await_handle) {
  return await_handle.owner != (uintptr_t)0 && await_handle.slot != 0u &&
         await_handle.generation != 0u && await_handle.reserved == 0u;
}

static uint64_t turbo_coro_executor_pending(const turbo_coro_executor_t *executor) {
  uint64_t submitted = atomic_load(&executor->submitted_tasks);
  uint64_t settled =
      atomic_load(&executor->completed_tasks) + atomic_load(&executor->cancelled_tasks);
  return submitted >= settled ? submitted - settled : 0u;
}

static void turbo_coro_executor_notify_progress(turbo_coro_executor_t *executor) {
  turbo_mutex_lock(&executor->wait_mutex);
  if (turbo_coro_executor_pending(executor) == 0u) turbo_cond_broadcast(&executor->all_done);
  turbo_mutex_unlock(&executor->wait_mutex);
}

static void turbo_coro_executor_update_peak(atomic_uint_fast64_t *peak, uint64_t candidate) {
  uint_fast64_t seen = atomic_load(peak);
  while (candidate > seen && !atomic_compare_exchange_weak(peak, &seen, (uint_fast64_t)candidate)) {
  }
}

static void turbo_coro_executor_wake_shards(turbo_coro_executor_t *executor) {
  if (executor == NULL || executor->shards == NULL) return;
  for (size_t index = 0u; index < executor->worker_count; ++index) {
    turbo_coro_executor_shard_t *shard = &executor->shards[index];
    turbo_mutex_lock(&shard->mutex);
    turbo_cond_broadcast(&shard->work_available);
    turbo_cond_broadcast(&shard->queue_space);
    turbo_mutex_unlock(&shard->mutex);
  }
}

static void turbo_coro_executor_stop_internal(turbo_coro_executor_t *executor) {
  atomic_store(&executor->accepting, 0);
  atomic_store(&executor->shutdown, 1);
  turbo_coro_executor_wake_shards(executor);
  turbo_coro_executor_notify_progress(executor);
}

/* shard->mutex protects await slots, their free list, and wake queue depth. */
static turbo_coro_executor_await_slot_t *
turbo_coro_executor_find_await_locked(turbo_coro_executor_shard_t *shard,
                                      turbo_coro_executor_await_t await_handle) {
  turbo_coro_executor_await_slot_t *slot;
  if (shard == NULL || await_handle.slot == 0u || await_handle.slot > shard->await_capacity)
    return NULL;
  slot = &shard->await_slots[await_handle.slot - 1u];
  return slot->active && slot->generation == await_handle.generation ? slot : NULL;
}

static void turbo_coro_executor_release_await_locked(turbo_coro_executor_shard_t *shard,
                                                     uint32_t index) {
  turbo_coro_executor_await_slot_t *slot = &shard->await_slots[index];
  turbo_coro_executor_t *executor = shard->executor;
  const uint32_t generation = slot->generation;
  const int was_waiting = slot->waiting;

  memset(slot, 0, sizeof(*slot));
  slot->generation = generation;
  shard->free_await_slots[shard->free_await_count++] = index;
  atomic_fetch_sub(&shard->active_await_count, 1u);
  atomic_fetch_sub(&executor->active_awaits, 1u);
  if (was_waiting) atomic_fetch_sub(&executor->waiting_awaits, 1u);
}

static void turbo_coro_executor_release_coro_await(turbo_coro_executor_shard_t *shard,
                                                   coro_t *coroutine) {
  if (atomic_load(&shard->active_await_count) == 0u) return;
  turbo_mutex_lock(&shard->mutex);
  for (uint32_t index = 0u; index < shard->await_capacity; ++index) {
    turbo_coro_executor_await_slot_t *slot = &shard->await_slots[index];
    if (slot->active && slot->coroutine == coroutine) {
      turbo_coro_executor_release_await_locked(shard, index);
      break;
    }
  }
  turbo_mutex_unlock(&shard->mutex);
}

static void turbo_coro_executor_finish_coro(coro_t *coroutine, void *arg) {
  turbo_coro_executor_shard_t *shard = (turbo_coro_executor_shard_t *)arg;
  turbo_coro_executor_t *executor = shard->executor;

  turbo_coro_executor_release_coro_await(shard, coroutine);
  turbo_coro_pool_release(shard->pool, coroutine);
  atomic_fetch_sub(&executor->active_tasks, 1u);
  atomic_fetch_add(&executor->completed_tasks, 1u);
  turbo_coro_executor_notify_progress(executor);
}

static void turbo_coro_executor_run_coro(coro_t *coroutine, void *arg) {
  turbo_coro_executor_task_t task;
  (void)arg;

  if (coro_pop(coroutine, &task, sizeof(task)) != 0) return;
  task.run(coroutine, task.arg);
  if (task.finalize != NULL) task.finalize(task.arg);
}

static void turbo_coro_executor_cancel_task(turbo_coro_executor_shard_t *shard,
                                            const turbo_coro_executor_task_t *task, int status) {
  turbo_coro_executor_t *executor = shard->executor;
  if (task->cancel != NULL) task->cancel(task->arg, status);
  if (task->finalize != NULL) task->finalize(task->arg);
  atomic_fetch_add(&executor->cancelled_tasks, 1u);
  turbo_coro_executor_notify_progress(executor);
}

static int turbo_coro_executor_take_task(turbo_coro_executor_shard_t *shard,
                                         turbo_coro_executor_task_t *task) {
  disruptor_cursor_t available;
  disruptor_cursor_t cursor;
  const turbo_coro_executor_task_t *entry;

  turbo_mutex_lock(&shard->mutex);
  if (shard->queued_depth == 0u) {
    turbo_mutex_unlock(&shard->mutex);
    return 0;
  }

  available.sequence = shard->next_sequence;
  /* The broadcast wait reports the latest contiguous sequence. Read and
   * release only next_sequence so per-shard FIFO entries are never skipped. */
  if (!disruptor_consumer_wait_for_nonblocking_for(shard->queue, &shard->consumer, &available)) {
    turbo_mutex_unlock(&shard->mutex);
    return 0;
  }

  cursor.sequence = shard->next_sequence;
  entry = (const turbo_coro_executor_task_t *)disruptor_show_entry(shard->queue, &cursor);
  if (entry == NULL || entry->run == NULL) {
    turbo_mutex_unlock(&shard->mutex);
    return 0;
  }

  *task = *entry;
  disruptor_consumer_release_entry(shard->queue, &shard->consumer, &cursor);
  shard->next_sequence++;
  shard->queued_depth--;
  atomic_fetch_sub(&shard->executor->queued_tasks, 1u);
  turbo_cond_broadcast(&shard->queue_space);
  turbo_mutex_unlock(&shard->mutex);
  return 1;
}

static int turbo_coro_executor_dispatch_wake(turbo_coro_executor_shard_t *shard) {
  disruptor_cursor_t available;
  disruptor_cursor_t cursor;
  const turbo_coro_executor_await_t *entry;
  turbo_coro_executor_await_t await_handle;
  turbo_coro_executor_await_slot_t *slot;

  turbo_mutex_lock(&shard->mutex);
  if (shard->wake_depth == 0u) {
    turbo_mutex_unlock(&shard->mutex);
    return 0;
  }

  available.sequence = shard->next_wake_sequence;
  if (!disruptor_consumer_wait_for_nonblocking_for(shard->wake_queue, &shard->wake_consumer,
                                                   &available)) {
    turbo_mutex_unlock(&shard->mutex);
    return 0;
  }

  cursor.sequence = shard->next_wake_sequence;
  entry = (const turbo_coro_executor_await_t *)disruptor_show_entry(shard->wake_queue, &cursor);
  if (entry == NULL) {
    turbo_mutex_unlock(&shard->mutex);
    return 0;
  }

  await_handle = *entry;
  disruptor_consumer_release_entry(shard->wake_queue, &shard->wake_consumer, &cursor);
  shard->next_wake_sequence++;
  shard->wake_depth--;

  slot = turbo_coro_executor_find_await_locked(shard, await_handle);
  if (slot != NULL && slot->waiting && slot->completion_ready)
    coro_set_waiting_for_io(slot->coroutine, 0);
  turbo_mutex_unlock(&shard->mutex);
  return 1;
}

static void turbo_coro_executor_start_task(turbo_coro_executor_shard_t *shard,
                                           const turbo_coro_executor_task_t *task) {
  turbo_coro_executor_t *executor = shard->executor;
  coro_t *coroutine = turbo_coro_pool_acquire(shard->pool, turbo_coro_executor_run_coro, shard);

  if (coroutine == NULL) {
    turbo_coro_executor_cancel_task(shard, task, TURBO_ENOMEM);
    return;
  }
  coro_set_data(coroutine, NULL);
  if (coro_push(coroutine, task, sizeof(*task)) != 0) {
    (void)turbo_coro_pool_abandon(shard->pool, coroutine);
    turbo_coro_executor_cancel_task(shard, task, TURBO_ENOBUFS);
    return;
  }

  coro_set_cleanup(coroutine, turbo_coro_executor_finish_coro, shard);
  coro_scheduler_adopt(shard->scheduler, coroutine);
  atomic_fetch_add(&executor->started_tasks, 1u);
  atomic_fetch_add(&executor->active_tasks, 1u);
}

static int turbo_coro_executor_shard_should_exit(turbo_coro_executor_shard_t *shard) {
  return atomic_load(&shard->executor->shutdown) && shard->queued_depth == 0u &&
         shard->wake_depth == 0u && turbo_coro_pool_active_count(shard->pool) == 0u;
}

static void turbo_coro_executor_report_startup(turbo_coro_executor_shard_t *shard, int failed) {
  turbo_coro_executor_t *executor = shard->executor;
  turbo_mutex_lock(&executor->startup_mutex);
  if (failed) executor->startup_failed = 1;
  executor->startup_count++;
  turbo_cond_broadcast(&executor->startup_done);
  turbo_mutex_unlock(&executor->startup_mutex);

  if (failed) turbo_coro_executor_stop_internal(executor);
}

static void turbo_coro_executor_worker(void *arg) {
  turbo_coro_executor_shard_t *shard = (turbo_coro_executor_shard_t *)arg;
  turbo_coro_executor_t *executor = shard->executor;
  turbo_coro_executor_t *previous_executor = turbo_coro_executor_tls;
  size_t previous_shard = turbo_coro_executor_shard_tls;
  int startup_failed = 0;
  int task_consumer_registered = 0;
  int wake_consumer_registered = 0;

  /* One persistent backing-pool task owns this scheduler, pool, and consumer
   * until drain completes. No live frame crosses this thread boundary. */
  turbo_coro_executor_tls = executor;
  turbo_coro_executor_shard_tls = shard->index;
  shard->scheduler = coro_scheduler_create();
  shard->pool = turbo_coro_pool_create(&executor->pool_config);
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

  turbo_coro_executor_report_startup(shard, startup_failed);

  if (!startup_failed) {
    for (;;) {
      int progressed = 0;

      while (turbo_coro_executor_dispatch_wake(shard))
        progressed = 1;

      while (turbo_coro_pool_active_count(shard->pool) < executor->pool_config.max_capacity) {
        turbo_coro_executor_task_t task;
        if (!turbo_coro_executor_take_task(shard, &task)) break;
        turbo_coro_executor_start_task(shard, &task);
        progressed = 1;
      }

      if (coro_scheduler_has_ready(shard->scheduler)) {
        (void)coro_scheduler_tick(shard->scheduler);
        progressed = 1;
      }

      turbo_mutex_lock(&shard->mutex);
      if (turbo_coro_executor_shard_should_exit(shard)) {
        turbo_mutex_unlock(&shard->mutex);
        break;
      }
      if (!progressed) {
        while (!turbo_coro_executor_shard_should_exit(shard) && shard->wake_depth == 0u &&
               !(shard->queued_depth > 0u &&
                 turbo_coro_pool_active_count(shard->pool) < executor->pool_config.max_capacity) &&
               !coro_scheduler_has_ready(shard->scheduler))
          turbo_cond_wait(&shard->work_available, &shard->mutex);
      }
      turbo_mutex_unlock(&shard->mutex);
    }
  }

  if (wake_consumer_registered)
    disruptor_consumer_unregister(shard->wake_queue, &shard->wake_consumer);
  if (task_consumer_registered) disruptor_consumer_unregister(shard->queue, &shard->consumer);

  coro_scheduler_destroy(shard->scheduler);
  turbo_coro_pool_destroy(shard->pool);
  shard->scheduler = NULL;
  shard->pool = NULL;
  turbo_coro_executor_tls = previous_executor;
  turbo_coro_executor_shard_tls = previous_shard;
}

static int turbo_coro_executor_config_valid(const turbo_coro_executor_config_t *config,
                                            size_t *worker_count) {
  int detected_workers;

  if (config == NULL || worker_count == NULL) return 0;
  *worker_count = config->worker_count;
  if (*worker_count == 0u) {
    detected_workers = turbo_cpu_count();
    if (detected_workers <= 0) return 0;
    *worker_count = (size_t)detected_workers;
  }

  if (*worker_count == 0u || *worker_count > (size_t)INT_MAX ||
      *worker_count > SIZE_MAX / sizeof(turbo_coro_executor_shard_t) ||
      !turbo_coro_executor_is_power_of_two(config->queue_capacity_per_worker) ||
      config->queue_capacity_per_worker > (size_t)INT64_MAX ||
      config->coroutine_pool.max_capacity == 0u ||
      config->coroutine_pool.max_capacity > (size_t)INT_MAX ||
      config->coroutine_pool.initial_capacity > config->coroutine_pool.max_capacity ||
      (config->coroutine_pool.storage_size != 0u &&
       config->coroutine_pool.storage_size < sizeof(turbo_coro_executor_task_t)) ||
      ((config->coroutine_pool.alloc_fn == NULL) != (config->coroutine_pool.free_fn == NULL)))
    return 0;

  return 1;
}

static void turbo_coro_executor_destroy_storage(turbo_coro_executor_t *executor) {
  if (executor == NULL) return;
  if (executor->shards != NULL) {
    for (size_t index = 0u; index < executor->worker_count; ++index) {
      turbo_coro_executor_shard_t *shard = &executor->shards[index];
      disruptor_destroy(shard->queue);
      disruptor_destroy(shard->wake_queue);
      free(shard->free_await_slots);
      free(shard->await_slots);
      turbo_mutex_destroy(&shard->mutex);
      turbo_cond_destroy(&shard->work_available);
      turbo_cond_destroy(&shard->queue_space);
    }
  }
  free(executor->shards);
  turbo_mutex_destroy(&executor->startup_mutex);
  turbo_cond_destroy(&executor->startup_done);
  turbo_mutex_destroy(&executor->wait_mutex);
  turbo_cond_destroy(&executor->all_done);
  free(executor);
}

turbo_coro_executor_t *turbo_coro_executor_create(const turbo_coro_executor_config_t *config) {
  turbo_coro_executor_config_t defaults = TURBO_CORO_EXECUTOR_CONFIG_DEFAULT;
  turbo_coro_executor_t *executor;
  turbo_threadpool_config_t worker_config;
  size_t worker_count;

  if (config == NULL) config = &defaults;
  if (!turbo_coro_executor_config_valid(config, &worker_count)) return NULL;

  executor = (turbo_coro_executor_t *)calloc(1, sizeof(*executor));
  if (executor == NULL) return NULL;
  executor->worker_count = worker_count;
  executor->queue_capacity_per_worker = config->queue_capacity_per_worker;
  executor->pool_config = config->coroutine_pool;
  atomic_store(&executor->accepting, 1);

  turbo_mutex_init(&executor->startup_mutex);
  turbo_cond_init(&executor->startup_done);
  turbo_mutex_init(&executor->wait_mutex);
  turbo_cond_init(&executor->all_done);
  if (executor->startup_mutex == NULL || executor->startup_done == NULL ||
      executor->wait_mutex == NULL || executor->all_done == NULL) {
    turbo_coro_executor_destroy_storage(executor);
    return NULL;
  }

  executor->shards = (turbo_coro_executor_shard_t *)calloc(worker_count, sizeof(*executor->shards));
  if (executor->shards == NULL) {
    turbo_coro_executor_destroy_storage(executor);
    return NULL;
  }

  for (size_t index = 0u; index < worker_count; ++index) {
    turbo_coro_executor_shard_t *shard = &executor->shards[index];
    size_t wake_capacity;
    disruptor_config_t queue_config = {
        sizeof(turbo_coro_executor_task_t),
        (uint64_t)executor->queue_capacity_per_worker,
        1u,
        DISRUPTOR_MODE_BROADCAST,
    };
    disruptor_config_t wake_queue_config;
    if (!turbo_coro_executor_next_power_of_two(executor->pool_config.max_capacity,
                                               &wake_capacity)) {
      turbo_coro_executor_destroy_storage(executor);
      return NULL;
    }
    wake_queue_config = (disruptor_config_t){sizeof(turbo_coro_executor_await_t),
                                             (uint64_t)wake_capacity, 1u, DISRUPTOR_MODE_BROADCAST};
    shard->executor = executor;
    shard->index = index;
    shard->await_capacity = executor->pool_config.max_capacity;
    shard->free_await_count = shard->await_capacity;
    shard->wake_capacity = wake_capacity;
    turbo_mutex_init(&shard->mutex);
    turbo_cond_init(&shard->work_available);
    turbo_cond_init(&shard->queue_space);
    shard->queue = disruptor_create(&queue_config);
    shard->wake_queue = disruptor_create(&wake_queue_config);
    shard->await_slots = (turbo_coro_executor_await_slot_t *)calloc(shard->await_capacity,
                                                                    sizeof(*shard->await_slots));
    shard->free_await_slots =
        (uint32_t *)calloc(shard->await_capacity, sizeof(*shard->free_await_slots));
    if (shard->mutex == NULL || shard->work_available == NULL || shard->queue_space == NULL ||
        shard->queue == NULL || shard->wake_queue == NULL || shard->await_slots == NULL ||
        shard->free_await_slots == NULL) {
      turbo_coro_executor_destroy_storage(executor);
      return NULL;
    }
    for (size_t slot = 0u; slot < shard->await_capacity; ++slot)
      shard->free_await_slots[slot] = (uint32_t)(shard->await_capacity - slot - 1u);
  }

  worker_config.num_threads = (int)worker_count;
  worker_config.queue_capacity = worker_count;
  executor->workers = turbo_threadpool_create_with_config(&worker_config);
  if (executor->workers == NULL) {
    turbo_coro_executor_destroy_storage(executor);
    return NULL;
  }

  for (size_t index = 0u; index < worker_count; ++index) {
    if (turbo_threadpool_submit(executor->workers, turbo_coro_executor_worker,
                                &executor->shards[index]) != TURBO_OK) {
      turbo_coro_executor_stop_internal(executor);
      turbo_threadpool_shutdown(executor->workers);
      turbo_threadpool_destroy(executor->workers);
      executor->workers = NULL;
      turbo_coro_executor_destroy_storage(executor);
      return NULL;
    }
  }

  turbo_mutex_lock(&executor->startup_mutex);
  while (executor->startup_count < executor->worker_count)
    turbo_cond_wait(&executor->startup_done, &executor->startup_mutex);
  turbo_mutex_unlock(&executor->startup_mutex);

  if (executor->startup_failed) {
    turbo_threadpool_shutdown(executor->workers);
    turbo_threadpool_destroy(executor->workers);
    executor->workers = NULL;
    turbo_coro_executor_destroy_storage(executor);
    return NULL;
  }

  return executor;
}

static int turbo_coro_executor_submit_internal(turbo_coro_executor_t *executor, size_t shard_index,
                                               const turbo_coro_executor_task_t *task,
                                               int blocking) {
  turbo_coro_executor_shard_t *shard;
  disruptor_cursor_t cursor = {0};
  turbo_coro_executor_task_t *entry;
  uint64_t queued;

  if (executor == NULL) return TURBO_EINVAL;
  if (task == NULL || task->run == NULL || shard_index >= executor->worker_count) {
    atomic_fetch_add(&executor->rejected_tasks, 1u);
    return TURBO_EINVAL;
  }

  shard = &executor->shards[shard_index];
  turbo_mutex_lock(&shard->mutex);
  while (atomic_load(&executor->accepting) &&
         shard->queued_depth >= executor->queue_capacity_per_worker) {
    if (!blocking || turbo_coro_executor_tls == executor) {
      turbo_mutex_unlock(&shard->mutex);
      atomic_fetch_add(&executor->rejected_tasks, 1u);
      return blocking && turbo_coro_executor_tls == executor ? TURBO_EBUSY : TURBO_ENOBUFS;
    }
    turbo_cond_wait(&shard->queue_space, &shard->mutex);
  }

  if (!atomic_load(&executor->accepting)) {
    turbo_mutex_unlock(&shard->mutex);
    atomic_fetch_add(&executor->rejected_tasks, 1u);
    return TURBO_ESHUTDOWN;
  }
  if (!disruptor_publisher_try_claim(shard->queue, &cursor)) {
    turbo_mutex_unlock(&shard->mutex);
    atomic_fetch_add(&executor->rejected_tasks, 1u);
    return TURBO_ENOBUFS;
  }

  entry = (turbo_coro_executor_task_t *)disruptor_acquire_entry(shard->queue, &cursor);
  *entry = *task;
  disruptor_publisher_commit_entry_blocking(shard->queue, &cursor);
  shard->queued_depth++;
  queued = atomic_fetch_add(&executor->queued_tasks, 1u) + 1u;
  atomic_fetch_add(&executor->submitted_tasks, 1u);
  turbo_coro_executor_update_peak(&executor->peak_queued_tasks, queued);
  turbo_cond_signal(&shard->work_available);
  turbo_mutex_unlock(&shard->mutex);
  return TURBO_OK;
}

int turbo_coro_executor_submit_to(turbo_coro_executor_t *executor, size_t shard,
                                  const turbo_coro_executor_task_t *task) {
  return turbo_coro_executor_submit_internal(executor, shard, task, 1);
}

int turbo_coro_executor_try_submit_to(turbo_coro_executor_t *executor, size_t shard,
                                      const turbo_coro_executor_task_t *task) {
  return turbo_coro_executor_submit_internal(executor, shard, task, 0);
}

int turbo_coro_executor_submit(turbo_coro_executor_t *executor,
                               const turbo_coro_executor_task_t *task) {
  size_t shard;
  if (executor == NULL) return TURBO_EINVAL;
  shard = (size_t)(atomic_fetch_add(&executor->round_robin, 1u) % executor->worker_count);
  return turbo_coro_executor_submit_internal(executor, shard, task, 1);
}

int turbo_coro_executor_try_submit(turbo_coro_executor_t *executor,
                                   const turbo_coro_executor_task_t *task) {
  size_t shard;
  if (executor == NULL) return TURBO_EINVAL;
  shard = (size_t)(atomic_fetch_add(&executor->round_robin, 1u) % executor->worker_count);
  return turbo_coro_executor_submit_internal(executor, shard, task, 0);
}

static turbo_coro_executor_shard_t *turbo_coro_executor_current_context(coro_t **coroutine) {
  turbo_coro_executor_shard_t *shard;
  coro_t *running;
  if (coroutine != NULL) *coroutine = NULL;
  if (turbo_coro_executor_tls == NULL ||
      turbo_coro_executor_shard_tls >= turbo_coro_executor_tls->worker_count)
    return NULL;
  shard = &turbo_coro_executor_tls->shards[turbo_coro_executor_shard_tls];
  running = coro_running();
  if (running == NULL || !coro_is_scheduled(running) ||
      coro_current_scheduler() != shard->scheduler)
    return NULL;
  if (coroutine != NULL) *coroutine = running;
  return shard;
}

int turbo_coro_executor_yield(void) {
  if (turbo_coro_executor_current_context(NULL) == NULL) return TURBO_EINVAL;
  return coro_yield() == 0 ? TURBO_OK : TURBO_EIO;
}

int turbo_coro_executor_await_begin(turbo_coro_executor_await_t *out_await) {
  turbo_coro_executor_shard_t *shard;
  turbo_coro_executor_await_slot_t *slot;
  coro_t *coroutine;
  uint32_t index;
  uint32_t generation;

  if (out_await != NULL) *out_await = (turbo_coro_executor_await_t){0};
  shard = turbo_coro_executor_current_context(&coroutine);
  if (shard == NULL || out_await == NULL) return TURBO_EINVAL;

  turbo_mutex_lock(&shard->mutex);
  for (size_t slot_index = 0u; slot_index < shard->await_capacity; ++slot_index) {
    slot = &shard->await_slots[slot_index];
    if (slot->active && slot->coroutine == coroutine) {
      turbo_mutex_unlock(&shard->mutex);
      return TURBO_EBUSY;
    }
  }
  if (shard->free_await_count == 0u) {
    turbo_mutex_unlock(&shard->mutex);
    return TURBO_ENOBUFS;
  }

  index = shard->free_await_slots[--shard->free_await_count];
  slot = &shard->await_slots[index];
  generation = turbo_coro_executor_next_generation(slot->generation);
  memset(slot, 0, sizeof(*slot));
  slot->coroutine = coroutine;
  slot->generation = generation;
  slot->active = 1;
  *out_await = (turbo_coro_executor_await_t){(uintptr_t)shard->executor, (uint32_t)shard->index,
                                             index + 1u, generation, 0u};
  atomic_fetch_add(&shard->active_await_count, 1u);
  atomic_fetch_add(&shard->executor->active_awaits, 1u);
  turbo_mutex_unlock(&shard->mutex);
  return TURBO_OK;
}

int turbo_coro_executor_await_complete(turbo_coro_executor_t *executor,
                                       turbo_coro_executor_await_t await_handle, int status) {
  turbo_coro_executor_shard_t *shard;
  turbo_coro_executor_await_slot_t *slot;
  disruptor_cursor_t cursor = {0};
  turbo_coro_executor_await_t *entry;

  if (executor == NULL || !turbo_coro_executor_await_handle_well_formed(await_handle) ||
      await_handle.owner != (uintptr_t)executor || await_handle.shard >= executor->worker_count)
    return TURBO_EINVAL;
  shard = &executor->shards[await_handle.shard];

  turbo_mutex_lock(&shard->mutex);
  slot = turbo_coro_executor_find_await_locked(shard, await_handle);
  if (slot == NULL) {
    turbo_mutex_unlock(&shard->mutex);
    return TURBO_ENOENT;
  }
  if (slot->completion_ready) {
    turbo_mutex_unlock(&shard->mutex);
    return TURBO_EALREADY;
  }

  if (slot->waiting) {
    if (shard->wake_depth >= shard->wake_capacity ||
        !disruptor_publisher_try_claim(shard->wake_queue, &cursor)) {
      turbo_mutex_unlock(&shard->mutex);
      return TURBO_ENOBUFS;
    }
    entry = (turbo_coro_executor_await_t *)disruptor_acquire_entry(shard->wake_queue, &cursor);
    *entry = await_handle;
    disruptor_publisher_commit_entry_blocking(shard->wake_queue, &cursor);
    shard->wake_depth++;
  }
  slot->completion_status = status;
  slot->completion_ready = 1;
  if (slot->waiting) turbo_cond_signal(&shard->work_available);
  turbo_mutex_unlock(&shard->mutex);
  return TURBO_OK;
}

int turbo_coro_executor_await(turbo_coro_executor_await_t await_handle, int *out_status) {
  turbo_coro_executor_shard_t *shard;
  turbo_coro_executor_await_slot_t *slot;
  coro_t *coroutine;
  uint32_t index;
  int status;

  if (out_status != NULL) *out_status = 0;
  shard = turbo_coro_executor_current_context(&coroutine);
  if (shard == NULL || out_status == NULL ||
      !turbo_coro_executor_await_handle_well_formed(await_handle) ||
      await_handle.owner != (uintptr_t)shard->executor || await_handle.shard != shard->index)
    return TURBO_EINVAL;
  index = await_handle.slot - 1u;

  turbo_mutex_lock(&shard->mutex);
  slot = turbo_coro_executor_find_await_locked(shard, await_handle);
  if (slot == NULL) {
    turbo_mutex_unlock(&shard->mutex);
    return TURBO_ENOENT;
  }
  if (slot->coroutine != coroutine) {
    turbo_mutex_unlock(&shard->mutex);
    return TURBO_EINVAL;
  }
  if (slot->completion_ready) {
    status = slot->completion_status;
    turbo_coro_executor_release_await_locked(shard, index);
    turbo_mutex_unlock(&shard->mutex);
    *out_status = status;
    return TURBO_OK;
  }
  if (slot->waiting) {
    turbo_mutex_unlock(&shard->mutex);
    return TURBO_EALREADY;
  }

  slot->waiting = 1;
  atomic_fetch_add(&shard->executor->waiting_awaits, 1u);
  coro_set_waiting_for_io(coroutine, 1);
  turbo_mutex_unlock(&shard->mutex);

  if (coro_yield() != 0) {
    turbo_mutex_lock(&shard->mutex);
    slot = turbo_coro_executor_find_await_locked(shard, await_handle);
    if (slot != NULL) {
      coro_set_waiting_for_io(coroutine, 0);
      turbo_coro_executor_release_await_locked(shard, index);
    }
    turbo_mutex_unlock(&shard->mutex);
    return TURBO_EIO;
  }

  turbo_mutex_lock(&shard->mutex);
  slot = turbo_coro_executor_find_await_locked(shard, await_handle);
  if (slot == NULL || slot->coroutine != coroutine || !slot->completion_ready) {
    if (slot != NULL) turbo_coro_executor_release_await_locked(shard, index);
    turbo_mutex_unlock(&shard->mutex);
    return TURBO_EPROTO;
  }
  status = slot->completion_status;
  turbo_coro_executor_release_await_locked(shard, index);
  turbo_mutex_unlock(&shard->mutex);
  *out_status = status;
  return TURBO_OK;
}

int turbo_coro_executor_await_abort(turbo_coro_executor_await_t await_handle) {
  turbo_coro_executor_shard_t *shard;
  turbo_coro_executor_await_slot_t *slot;
  coro_t *coroutine;
  uint32_t index;

  shard = turbo_coro_executor_current_context(&coroutine);
  if (shard == NULL || !turbo_coro_executor_await_handle_well_formed(await_handle) ||
      await_handle.owner != (uintptr_t)shard->executor || await_handle.shard != shard->index)
    return TURBO_EINVAL;
  index = await_handle.slot - 1u;

  turbo_mutex_lock(&shard->mutex);
  slot = turbo_coro_executor_find_await_locked(shard, await_handle);
  if (slot == NULL) {
    turbo_mutex_unlock(&shard->mutex);
    return TURBO_ENOENT;
  }
  if (slot->coroutine != coroutine) {
    turbo_mutex_unlock(&shard->mutex);
    return TURBO_EINVAL;
  }
  if (slot->completion_ready) {
    turbo_mutex_unlock(&shard->mutex);
    return TURBO_EALREADY;
  }
  if (slot->waiting) {
    turbo_mutex_unlock(&shard->mutex);
    return TURBO_EBUSY;
  }
  turbo_coro_executor_release_await_locked(shard, index);
  turbo_mutex_unlock(&shard->mutex);
  return TURBO_OK;
}

int turbo_coro_executor_shutdown(turbo_coro_executor_t *executor) {
  if (executor == NULL) return TURBO_EINVAL;
  turbo_coro_executor_stop_internal(executor);
  return TURBO_OK;
}

int turbo_coro_executor_wait(turbo_coro_executor_t *executor) {
  if (executor == NULL) return TURBO_EINVAL;
  if (turbo_coro_executor_tls == executor) return TURBO_EBUSY;

  turbo_mutex_lock(&executor->wait_mutex);
  while (turbo_coro_executor_pending(executor) > 0u)
    turbo_cond_wait(&executor->all_done, &executor->wait_mutex);
  turbo_mutex_unlock(&executor->wait_mutex);
  return TURBO_OK;
}

int turbo_coro_executor_destroy(turbo_coro_executor_t *executor) {
  int status;
  if (executor == NULL) return TURBO_EINVAL;
  if (turbo_coro_executor_tls == executor) return TURBO_EBUSY;

  turbo_coro_executor_stop_internal(executor);
  status = turbo_coro_executor_wait(executor);
  if (status != TURBO_OK) return status;

  turbo_threadpool_shutdown(executor->workers);
  status = turbo_threadpool_wait_status(executor->workers);
  if (status != TURBO_OK) return status;
  turbo_threadpool_destroy(executor->workers);
  executor->workers = NULL;
  turbo_coro_executor_destroy_storage(executor);
  return TURBO_OK;
}

turbo_coro_executor_t *turbo_coro_executor_current(void) { return turbo_coro_executor_tls; }

size_t turbo_coro_executor_current_shard(const turbo_coro_executor_t *executor) {
  return executor != NULL && turbo_coro_executor_tls == executor ? turbo_coro_executor_shard_tls
                                                                 : SIZE_MAX;
}

void turbo_coro_executor_get_stats(const turbo_coro_executor_t *executor,
                                   turbo_coro_executor_stats_t *stats) {
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
