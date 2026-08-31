#include "cnet_callback.h"

#include <turbo/clock.h>
#include <turbo/disruptor.h>
#include <turbo/thread.h>
#include <turbo/thread_pool.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>

typedef struct cnet_callback_entry {
  cnet_callback_fn invoke;
  cnet_callback_fn finish;
  void *context;
  cnet_callback_release_fn release;
  void *release_context;
  uint64_t release_token;
  cnet_event_kind kind;
  cnet_session_handle session;
  cnet_event_state state;
  int status;
  cnet_session_stage stage;
  const void *data;
  size_t size;
} cnet_callback_entry;

typedef struct cnet_callback_lane {
  disruptor_t *ring;
  atomic_size_t live_jobs;
  atomic_size_t publisher_entrants;
  atomic_bool admission_open;
  atomic_bool settled;
  atomic_int *first_error;
} cnet_callback_lane;

typedef struct cnet_callback_workers_impl {
  cnet_callback_lane *lanes;
  turbo_threadpool_t *pool;
  size_t worker_count;
  size_t max_payload_bytes;
  atomic_int first_error;
  bool stopping;
  bool stopped;
} cnet_callback_workers_impl;

static cnet_callback_workers_impl *cnet_callback_get(cnet_callback_workers *workers) {
  return workers != NULL ? (cnet_callback_workers_impl *)workers->impl : NULL;
}

static bool cnet_callback_power_of_two(uint64_t value) {
  return value != 0u && (value & (value - 1u)) == 0u;
}

static bool cnet_callback_event_valid(const cnet_event *event) {
  if (event == NULL || !cnet_session_handle_valid(event->session)) return false;
  if (event->kind == CNET_EVENT_RECEIVE)
    return event->state == CNET_EVENT_STATE_NONE && event->status == TURBO_OK &&
           event->stage == CNET_SESSION_STAGE_NONE && (event->data != NULL || event->size == 0u);
  if (event->kind != CNET_EVENT_STATE || event->data != NULL || event->size != 0u) return false;
  if (event->state < CNET_EVENT_STATE_CONNECTED || event->state > CNET_EVENT_STATE_FAILED)
    return false;
  if (event->state == CNET_EVENT_STATE_FAILED)
    return event->status != TURBO_OK && event->stage != CNET_SESSION_STAGE_NONE;
  return event->status == TURBO_OK && event->stage == CNET_SESSION_STAGE_NONE;
}

static int cnet_callback_lane_keep_running(void *context) {
  cnet_callback_lane *lane = (cnet_callback_lane *)context;
  return atomic_load_explicit(&lane->admission_open, memory_order_acquire) ||
         atomic_load_explicit(&lane->live_jobs, memory_order_acquire) != 0u ||
         atomic_load_explicit(&lane->publisher_entrants, memory_order_acquire) != 0u;
}

static void cnet_callback_lane_task(void *context) {
  cnet_callback_lane *lane = (cnet_callback_lane *)context;
  disruptor_cursor_t cursor = {0};

  while (disruptor_worker_claim_wait(lane->ring, &cursor, cnet_callback_lane_keep_running, lane)) {
    const cnet_callback_entry *entry =
        (const cnet_callback_entry *)disruptor_show_entry(lane->ring, &cursor);
    const cnet_callback_view view = {entry->kind,  entry->session, entry->state, entry->status,
                                     entry->stage, entry->data,    entry->size};
    int status = TURBO_OK;

    entry->invoke(entry->context, &view);
    if (entry->finish != NULL) entry->finish(entry->context, &view);
    if (entry->release != NULL)
      status = entry->release(entry->release_context, &view, entry->release_token);
    if (status != TURBO_OK) {
      int expected = TURBO_OK;
      (void)atomic_compare_exchange_strong_explicit(lane->first_error, &expected, status,
                                                    memory_order_acq_rel, memory_order_acquire);
    }
    disruptor_worker_release_entry(lane->ring, &cursor);
    atomic_fetch_sub_explicit(&lane->live_jobs, 1u, memory_order_release);
  }
  atomic_store_explicit(&lane->settled, true, memory_order_release);
}

static void cnet_callback_close_lanes(cnet_callback_lane *lanes, size_t count) {
  size_t index;
  for (index = 0u; index < count; ++index) {
    atomic_store_explicit(&lanes[index].admission_open, false, memory_order_release);
    disruptor_worker_wake_all(lanes[index].ring);
  }
}

static void cnet_callback_destroy_lanes(cnet_callback_lane *lanes, size_t count) {
  size_t index;
  if (lanes == NULL) return;
  for (index = 0u; index < count; ++index)
    if (lanes[index].ring != NULL) disruptor_destroy(lanes[index].ring);
  free(lanes);
}

int cnet_callback_workers_init(cnet_callback_workers *workers,
                               const cnet_callback_workers_config *config) {
  cnet_callback_workers_impl *impl;
  disruptor_config_t ring_config;
  turbo_threadpool_config_t pool_config;
  size_t entry_size;
  size_t lane_bytes;
  size_t initialized = 0u;
  size_t index;
  int status = TURBO_OK;

  if (workers == NULL || config == NULL) return TURBO_EINVAL;
  if (workers->impl != NULL) return TURBO_EALREADY;
  if (config->worker_count == 0u || config->worker_count > INT_MAX ||
      !cnet_callback_power_of_two(config->capacity_per_worker) || config->max_payload_bytes == 0u)
    return TURBO_EINVAL;
  if (config->worker_count > SIZE_MAX / sizeof(cnet_callback_lane)) return TURBO_ERANGE;
  entry_size = sizeof(cnet_callback_entry);
  if (config->capacity_per_worker > SIZE_MAX / entry_size) return TURBO_ERANGE;
  lane_bytes = (size_t)config->capacity_per_worker * entry_size;
  if (config->worker_count > SIZE_MAX / lane_bytes) return TURBO_ERANGE;

  impl = (cnet_callback_workers_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return TURBO_ENOMEM;
  impl->lanes = (cnet_callback_lane *)calloc(config->worker_count, sizeof(*impl->lanes));
  if (impl->lanes == NULL) {
    free(impl);
    return TURBO_ENOMEM;
  }
  impl->worker_count = config->worker_count;
  impl->max_payload_bytes = config->max_payload_bytes;
  atomic_init(&impl->first_error, TURBO_OK);
  ring_config =
      (disruptor_config_t){entry_size, config->capacity_per_worker, 1u, DISRUPTOR_MODE_WORKER_POOL};
  for (index = 0u; index < impl->worker_count; ++index) {
    cnet_callback_lane *lane = &impl->lanes[index];
    lane->ring = disruptor_create(&ring_config);
    if (lane->ring == NULL) {
      status = TURBO_ENOMEM;
      break;
    }
    atomic_init(&lane->live_jobs, 0u);
    atomic_init(&lane->publisher_entrants, 0u);
    atomic_init(&lane->admission_open, true);
    atomic_init(&lane->settled, false);
    lane->first_error = &impl->first_error;
    ++initialized;
  }
  if (status != TURBO_OK) {
    cnet_callback_destroy_lanes(impl->lanes, initialized);
    free(impl);
    return status;
  }

  pool_config = (turbo_threadpool_config_t){(int)impl->worker_count, impl->worker_count};
  impl->pool = turbo_threadpool_create_with_config(&pool_config);
  if (impl->pool == NULL) {
    cnet_callback_destroy_lanes(impl->lanes, initialized);
    free(impl);
    return TURBO_ENOMEM;
  }
  for (index = 0u; index < impl->worker_count; ++index) {
    status = turbo_threadpool_try_submit(impl->pool, cnet_callback_lane_task, &impl->lanes[index]);
    if (status != TURBO_OK) break;
  }
  if (status != TURBO_OK) {
    cnet_callback_close_lanes(impl->lanes, initialized);
    (void)turbo_threadpool_shutdown_with_policy(impl->pool, TURBO_THREADPOOL_SHUTDOWN_DRAIN);
    (void)turbo_threadpool_wait_status(impl->pool);
    turbo_threadpool_destroy(impl->pool);
    cnet_callback_destroy_lanes(impl->lanes, initialized);
    free(impl);
    return status;
  }
  workers->impl = impl;
  return TURBO_OK;
}

int cnet_callback_workers_publish(cnet_callback_workers *workers, const cnet_callback_job *job) {
  cnet_callback_workers_impl *impl = cnet_callback_get(workers);
  cnet_callback_lane *lane;
  disruptor_cursor_t cursor = {0};
  cnet_callback_entry *entry;

  if (impl == NULL || job == NULL || job->invoke == NULL ||
      (job->event.kind == CNET_EVENT_RECEIVE && job->release == NULL) ||
      !cnet_callback_event_valid(&job->event))
    return TURBO_EINVAL;
  if (job->event.size > impl->max_payload_bytes) return TURBO_EMSGSIZE;
  lane = &impl->lanes[(size_t)(job->serialization_key % impl->worker_count)];
  if (!atomic_load_explicit(&lane->admission_open, memory_order_acquire)) return TURBO_ESHUTDOWN;

  atomic_fetch_add_explicit(&lane->publisher_entrants, 1u, memory_order_acq_rel);
  if (!atomic_load_explicit(&lane->admission_open, memory_order_acquire)) {
    atomic_fetch_sub_explicit(&lane->publisher_entrants, 1u, memory_order_release);
    return TURBO_ESHUTDOWN;
  }
  if (!disruptor_publisher_try_claim(lane->ring, &cursor)) {
    atomic_fetch_sub_explicit(&lane->publisher_entrants, 1u, memory_order_release);
    return TURBO_ENOBUFS;
  }

  entry = (cnet_callback_entry *)disruptor_acquire_entry(lane->ring, &cursor);
  entry->invoke = job->invoke;
  entry->finish = job->finish;
  entry->context = job->context;
  entry->release = job->release;
  entry->release_context = job->release_context;
  entry->release_token = job->release_token;
  entry->kind = job->event.kind;
  entry->session = job->event.session;
  entry->state = job->event.state;
  entry->status = job->event.status;
  entry->stage = job->event.stage;
  entry->data = job->event.data;
  entry->size = job->event.size;
  atomic_fetch_add_explicit(&lane->live_jobs, 1u, memory_order_release);
  (void)disruptor_publisher_publish(lane->ring, &cursor);
  atomic_fetch_sub_explicit(&lane->publisher_entrants, 1u, memory_order_release);
  return TURBO_OK;
}

int cnet_callback_workers_stop(cnet_callback_workers *workers, uint32_t timeout_ms) {
  cnet_callback_workers_impl *impl = cnet_callback_get(workers);
  const uint64_t started_ms = turbo_monotonic_ms();
  size_t index;
  int status;

  if (impl == NULL) return TURBO_EINVAL;
  if (impl->stopped) return TURBO_EALREADY;
  if (!impl->stopping) {
    impl->stopping = true;
    cnet_callback_close_lanes(impl->lanes, impl->worker_count);
  }

  for (;;) {
    bool settled = true;
    for (index = 0u; index < impl->worker_count; ++index)
      if (!atomic_load_explicit(&impl->lanes[index].settled, memory_order_acquire)) {
        settled = false;
        break;
      }
    if (settled) break;
    if (turbo_monotonic_ms() - started_ms >= timeout_ms) return TURBO_ETIMEDOUT;
    turbo_sleep_ms(1u);
  }

  status = turbo_threadpool_shutdown_with_policy(impl->pool, TURBO_THREADPOOL_SHUTDOWN_DRAIN);
  if (status != TURBO_OK) return status;
  status = turbo_threadpool_wait_status(impl->pool);
  if (status != TURBO_OK) return status;
  impl->stopped = true;
  return atomic_load_explicit(&impl->first_error, memory_order_acquire);
}

int cnet_callback_workers_destroy(cnet_callback_workers *workers) {
  cnet_callback_workers_impl *impl = cnet_callback_get(workers);
  size_t index;

  if (workers == NULL) return TURBO_EINVAL;
  if (impl == NULL) return TURBO_OK;
  if (!impl->stopped) return TURBO_EBUSY;
  for (index = 0u; index < impl->worker_count; ++index)
    if (atomic_load_explicit(&impl->lanes[index].live_jobs, memory_order_acquire) != 0u ||
        atomic_load_explicit(&impl->lanes[index].publisher_entrants, memory_order_acquire) != 0u)
      return TURBO_EBUSY;

  turbo_threadpool_destroy(impl->pool);
  cnet_callback_destroy_lanes(impl->lanes, impl->worker_count);
  free(impl);
  workers->impl = NULL;
  return TURBO_OK;
}
