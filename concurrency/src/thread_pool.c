#include <turbo/disruptor.h>
#include <turbo/error_codes.h>
#include <turbo/thread.h>
#include <turbo/thread_pool.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct task_entry_s {
  turbo_task_fn fn;
  void *arg;
} task_entry_t;

typedef struct worker_context_s {
  turbo_threadpool_t *pool;
  int worker_id;
} worker_context_t;

struct turbo_threadpool_s {
  turbo_thread_t *threads;
  worker_context_t *workers;
  int num_threads;
  size_t queue_capacity;
  disruptor_t *queue;

  atomic_int accepting;
  atomic_int shutdown;
  _Atomic int64_t queued_depth;
  _Atomic int64_t tasks_submitted;
  _Atomic int64_t tasks_started;
  _Atomic int64_t tasks_completed;
  _Atomic int64_t tasks_rejected;
  _Atomic int64_t peak_pending_tasks;

  turbo_mutex_t park_mutex;
  turbo_cond_t task_available;
  turbo_cond_t queue_space;
  turbo_mutex_t wait_mutex;
  turbo_cond_t all_done;
};

#define TURBO_THREADPOOL_DEFAULT_QUEUE_CAPACITY 4096U

static uint64_t turbo_threadpool_round_up_pow2(size_t value) {
  uint64_t rounded = 1U;

  if (value == 0U) return 0U;
  while (rounded < (uint64_t)value) {
    if (rounded > (UINT64_MAX >> 1U)) return 0U;
    rounded <<= 1U;
  }
  return rounded;
}

static int64_t turbo_threadpool_pending_tasks(const turbo_threadpool_t *pool) {
  if (pool == NULL) return 0;
  return atomic_load(&pool->tasks_submitted) -
         atomic_load(&pool->tasks_completed);
}

static void turbo_threadpool_update_peak_pending(turbo_threadpool_t *pool,
                                                 int64_t candidate) {
  int64_t seen = atomic_load(&pool->peak_pending_tasks);
  while (candidate > seen && !atomic_compare_exchange_weak(
                                 &pool->peak_pending_tasks, &seen, candidate)) {
  }
}

static void turbo_threadpool_notify_progress(turbo_threadpool_t *pool) {
  if (pool == NULL) return;
  turbo_mutex_lock(&pool->wait_mutex);
  if (turbo_threadpool_pending_tasks(pool) <= 0)
    turbo_cond_broadcast(&pool->all_done);
  turbo_mutex_unlock(&pool->wait_mutex);
}

static void turbo_threadpool_finish_task(turbo_threadpool_t *pool) {
  atomic_fetch_add(&pool->tasks_completed, 1);
  turbo_threadpool_notify_progress(pool);
}

static void turbo_threadpool_signal_task_available(turbo_threadpool_t *pool) {
  turbo_mutex_lock(&pool->park_mutex);
  turbo_cond_signal(&pool->task_available);
  turbo_mutex_unlock(&pool->park_mutex);
}

static void turbo_threadpool_signal_queue_space(turbo_threadpool_t *pool) {
  turbo_mutex_lock(&pool->park_mutex);
  turbo_cond_signal(&pool->queue_space);
  turbo_mutex_unlock(&pool->park_mutex);
}

static int turbo_threadpool_try_reserve_queue_slot(turbo_threadpool_t *pool,
                                                    int blocking) {
  int64_t depth;

  while (atomic_load(&pool->accepting) && !atomic_load(&pool->shutdown)) {
    depth = atomic_load(&pool->queued_depth);
    while (depth < (int64_t)pool->queue_capacity) {
      if (atomic_compare_exchange_weak(&pool->queued_depth, &depth, depth + 1))
        return 1;
    }

    if (!blocking) return 0;

    turbo_mutex_lock(&pool->park_mutex);
    while (atomic_load(&pool->queued_depth) >= (int64_t)pool->queue_capacity &&
           atomic_load(&pool->accepting) && !atomic_load(&pool->shutdown)) {
      turbo_cond_wait(&pool->queue_space, &pool->park_mutex);
    }
    turbo_mutex_unlock(&pool->park_mutex);
  }

  return 0;
}

static void turbo_threadpool_release_queue_slot(turbo_threadpool_t *pool) {
  atomic_fetch_sub(&pool->queued_depth, 1);
  turbo_threadpool_signal_queue_space(pool);
}

static void worker_entry(void *arg) {
  worker_context_t *ctx = (worker_context_t *)arg;
  turbo_threadpool_t *pool = ctx->pool;

  while (1) {
    disruptor_cursor_t cursor = {0};
    const task_entry_t *entry;

    if (!disruptor_worker_try_claim(pool->queue, &cursor)) {
      if (atomic_load(&pool->shutdown) &&
          turbo_threadpool_pending_tasks(pool) <= 0 &&
          atomic_load(&pool->queued_depth) <= 0) {
        break;
      }

      if (atomic_load(&pool->queued_depth) > 0) {
        turbo_thread_yield();
        continue;
      }

      turbo_mutex_lock(&pool->park_mutex);
      while (atomic_load(&pool->queued_depth) <= 0 &&
             !atomic_load(&pool->shutdown)) {
        turbo_cond_wait(&pool->task_available, &pool->park_mutex);
      }
      turbo_mutex_unlock(&pool->park_mutex);
      continue;
    }

    entry = (const task_entry_t *)disruptor_show_entry(pool->queue, &cursor);
    if (entry == NULL || entry->fn == NULL) {
      disruptor_worker_release_entry(pool->queue, &cursor);
      continue;
    }

    turbo_threadpool_release_queue_slot(pool);
    atomic_fetch_add(&pool->tasks_started, 1);
    entry->fn(entry->arg);
    disruptor_worker_release_entry(pool->queue, &cursor);
    turbo_threadpool_signal_queue_space(pool);
    turbo_threadpool_finish_task(pool);
  }

  turbo_threadpool_notify_progress(pool);
}

turbo_threadpool_t *
turbo_threadpool_create_with_config(const turbo_threadpool_config_t *config) {
  turbo_threadpool_t *pool;
  disruptor_config_t queue_config;
  int num_threads;
  size_t queue_capacity;
  uint64_t ring_capacity;

  if (config == NULL) return NULL;

  num_threads = config->num_threads;
  if (num_threads <= 0) num_threads = turbo_cpu_count();
  queue_capacity = config->queue_capacity > 0U
                       ? config->queue_capacity
                       : TURBO_THREADPOOL_DEFAULT_QUEUE_CAPACITY;
  if (queue_capacity == SIZE_MAX) return NULL;

  ring_capacity = turbo_threadpool_round_up_pow2(queue_capacity + 1U);
  if (ring_capacity == 0U || ring_capacity > (uint64_t)SIZE_MAX ||
      ring_capacity > (uint64_t)INT64_MAX)
    return NULL;

  pool = (turbo_threadpool_t *)calloc(1, sizeof(*pool));
  if (pool == NULL) return NULL;

  pool->num_threads = num_threads;
  pool->queue_capacity = queue_capacity;
  atomic_store(&pool->accepting, 1);
  atomic_store(&pool->shutdown, 0);
  atomic_store(&pool->queued_depth, 0);
  atomic_store(&pool->tasks_submitted, 0);
  atomic_store(&pool->tasks_started, 0);
  atomic_store(&pool->tasks_completed, 0);
  atomic_store(&pool->tasks_rejected, 0);
  atomic_store(&pool->peak_pending_tasks, 0);

  queue_config.entry_size = sizeof(task_entry_t);
  queue_config.capacity = ring_capacity;
  queue_config.consumer_capacity = 1U;
  queue_config.mode = DISRUPTOR_MODE_WORKER_POOL;
  pool->queue = disruptor_create(&queue_config);
  if (pool->queue == NULL) {
    free(pool);
    return NULL;
  }

  turbo_mutex_init(&pool->park_mutex);
  turbo_cond_init(&pool->task_available);
  turbo_cond_init(&pool->queue_space);
  turbo_mutex_init(&pool->wait_mutex);
  turbo_cond_init(&pool->all_done);

  pool->threads = (turbo_thread_t *)calloc((size_t)num_threads,
                                           sizeof(*pool->threads));
  pool->workers = (worker_context_t *)calloc((size_t)num_threads,
                                             sizeof(*pool->workers));
  if (pool->threads == NULL || pool->workers == NULL) {
    free(pool->threads);
    free(pool->workers);
    turbo_mutex_destroy(&pool->park_mutex);
    turbo_cond_destroy(&pool->task_available);
    turbo_cond_destroy(&pool->queue_space);
    turbo_mutex_destroy(&pool->wait_mutex);
    turbo_cond_destroy(&pool->all_done);
    disruptor_destroy(pool->queue);
    free(pool);
    return NULL;
  }

  for (int i = 0; i < num_threads; ++i) {
    pool->workers[i].pool = pool;
    pool->workers[i].worker_id = i;
    if (turbo_thread_create(&pool->threads[i], worker_entry,
                            &pool->workers[i]) != 0) {
      atomic_store(&pool->accepting, 0);
      atomic_store(&pool->shutdown, 1);
      for (int j = 0; j < i; ++j)
        (void)turbo_thread_join(&pool->threads[j]);
      free(pool->threads);
      free(pool->workers);
      turbo_mutex_destroy(&pool->park_mutex);
      turbo_cond_destroy(&pool->task_available);
      turbo_cond_destroy(&pool->queue_space);
      turbo_mutex_destroy(&pool->wait_mutex);
      turbo_cond_destroy(&pool->all_done);
      disruptor_destroy(pool->queue);
      free(pool);
      return NULL;
    }
  }

  return pool;
}

turbo_threadpool_t *turbo_threadpool_create(int num_threads) {
  turbo_threadpool_config_t config;
  config.num_threads = num_threads;
  config.queue_capacity = TURBO_THREADPOOL_DEFAULT_QUEUE_CAPACITY;
  return turbo_threadpool_create_with_config(&config);
}

void turbo_threadpool_shutdown(turbo_threadpool_t *pool) {
  if (pool == NULL) return;
  atomic_store(&pool->accepting, 0);
  atomic_store(&pool->shutdown, 1);
  turbo_mutex_lock(&pool->park_mutex);
  turbo_cond_broadcast(&pool->task_available);
  turbo_cond_broadcast(&pool->queue_space);
  turbo_mutex_unlock(&pool->park_mutex);
  turbo_threadpool_notify_progress(pool);
}

void turbo_threadpool_destroy(turbo_threadpool_t *pool) {
  if (pool == NULL) return;
  turbo_threadpool_shutdown(pool);
  for (int i = 0; i < pool->num_threads; ++i)
    (void)turbo_thread_join(&pool->threads[i]);

  turbo_mutex_destroy(&pool->park_mutex);
  turbo_cond_destroy(&pool->task_available);
  turbo_cond_destroy(&pool->queue_space);
  turbo_mutex_destroy(&pool->wait_mutex);
  turbo_cond_destroy(&pool->all_done);
  disruptor_destroy(pool->queue);
  free(pool->workers);
  free(pool->threads);
  free(pool);
}

static int turbo_threadpool_submit_internal(turbo_threadpool_t *pool,
                                            turbo_task_fn task,
                                            void *arg,
                                            int blocking) {
  disruptor_cursor_t cursor = {0};
  task_entry_t *entry;
  unsigned int wait_rounds = 0U;

  if (pool == NULL || task == NULL) return TURBO_EINVAL;
  if (!atomic_load(&pool->accepting) || atomic_load(&pool->shutdown)) {
    atomic_fetch_add(&pool->tasks_rejected, 1);
    return TURBO_ESHUTDOWN;
  }

  if (!turbo_threadpool_try_reserve_queue_slot(pool, blocking)) {
    atomic_fetch_add(&pool->tasks_rejected, 1);
    return (!atomic_load(&pool->accepting) || atomic_load(&pool->shutdown))
               ? TURBO_ESHUTDOWN
               : TURBO_ENOBUFS;
  }

  while (!disruptor_publisher_try_claim(pool->queue, &cursor)) {
    if (!blocking || !atomic_load(&pool->accepting) ||
        atomic_load(&pool->shutdown)) {
      turbo_threadpool_release_queue_slot(pool);
      atomic_fetch_add(&pool->tasks_rejected, 1);
      return (!atomic_load(&pool->accepting) || atomic_load(&pool->shutdown))
                 ? TURBO_ESHUTDOWN
                 : TURBO_ENOBUFS;
    }

    if ((++wait_rounds & 0xFFU) == 0U)
      turbo_sleep_ms(1);
    else
      turbo_thread_yield();
  }

  entry = (task_entry_t *)disruptor_acquire_entry(pool->queue, &cursor);
  entry->fn = task;
  entry->arg = arg;
  {
    int64_t submitted = atomic_fetch_add(&pool->tasks_submitted, 1) + 1;
    turbo_threadpool_update_peak_pending(
        pool, submitted - atomic_load(&pool->tasks_completed));
  }
  (void)disruptor_publisher_publish(pool->queue, &cursor);
  turbo_threadpool_signal_task_available(pool);
  return TURBO_OK;
}

int turbo_threadpool_submit(turbo_threadpool_t *pool,
                            turbo_task_fn task,
                            void *arg) {
  return turbo_threadpool_submit_internal(pool, task, arg, 1);
}

int turbo_threadpool_try_submit(turbo_threadpool_t *pool,
                                turbo_task_fn task,
                                void *arg) {
  return turbo_threadpool_submit_internal(pool, task, arg, 0);
}

void turbo_threadpool_wait(turbo_threadpool_t *pool) {
  if (pool == NULL) return;
  turbo_mutex_lock(&pool->wait_mutex);
  while (turbo_threadpool_pending_tasks(pool) > 0)
    turbo_cond_wait(&pool->all_done, &pool->wait_mutex);
  turbo_mutex_unlock(&pool->wait_mutex);
}

int turbo_threadpool_pending(turbo_threadpool_t *pool) {
  return (int)turbo_threadpool_pending_tasks(pool);
}

int turbo_threadpool_size(turbo_threadpool_t *pool) {
  return pool != NULL ? pool->num_threads : 0;
}

size_t turbo_threadpool_capacity(turbo_threadpool_t *pool) {
  return pool != NULL ? pool->queue_capacity : 0U;
}

int turbo_threadpool_is_accepting(turbo_threadpool_t *pool) {
  return pool != NULL ? atomic_load(&pool->accepting) : 0;
}

void turbo_threadpool_get_stats(turbo_threadpool_t *pool,
                                turbo_threadpool_stats_t *stats) {
  int64_t submitted;
  int64_t started;
  int64_t completed;

  if (pool == NULL || stats == NULL) return;
  memset(stats, 0, sizeof(*stats));

  submitted = atomic_load(&pool->tasks_submitted);
  started = atomic_load(&pool->tasks_started);
  completed = atomic_load(&pool->tasks_completed);

  stats->num_threads = pool->num_threads;
  stats->queue_capacity = pool->queue_capacity;
  stats->accepting = atomic_load(&pool->accepting);
  stats->submitted_tasks = submitted;
  stats->started_tasks = started;
  stats->completed_tasks = completed;
  stats->rejected_tasks = atomic_load(&pool->tasks_rejected);
  stats->queued_tasks = atomic_load(&pool->queued_depth);
  stats->active_tasks = started - completed;
  stats->pending_tasks = submitted - completed;
  stats->peak_pending_tasks = atomic_load(&pool->peak_pending_tasks);
}
