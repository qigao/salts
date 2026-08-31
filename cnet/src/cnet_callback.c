#include "cnet_callback.h"

#include <turbo/spsc_ring.h>

#include <turbo/clock.h>
#include <turbo/thread.h>
#include <turbo/thread_pool.h>

#include <limits.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { CNET_CALLBACK_MAX_BATCH_PER_CHANNEL = 32 };

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
  size_t size;
  unsigned char payload[];
} cnet_callback_entry;

typedef struct cnet_callback_workers_impl cnet_callback_workers_impl;

typedef struct cnet_callback_channel {
  turbo_spsc_ring ring;
  unsigned char *storage;
  size_t worker;
  atomic_size_t live_jobs;
  atomic_size_t publisher_entrants;
  atomic_bool admission_open;
} cnet_callback_channel;

typedef struct cnet_callback_worker {
  cnet_callback_workers_impl *owner;
  turbo_mutex_t wake_lock;
  turbo_cond_t wake_cond;
  atomic_bool wake_pending;
  atomic_bool settled;
  size_t index;
} cnet_callback_worker;

struct cnet_callback_workers_impl {
  cnet_callback_channel *channels;
  cnet_callback_worker *workers;
  turbo_threadpool_t *pool;
  size_t worker_count;
  size_t producer_count;
  uint64_t capacity_per_producer;
  size_t max_payload_bytes;
  size_t entry_stride;
  atomic_int first_error;
  bool stopping;
  bool stopped;
};

static cnet_callback_workers_impl *cnet_callback_get(cnet_callback_workers *workers) {
  return workers != NULL ? (cnet_callback_workers_impl *)workers->impl : NULL;
}

static bool cnet_callback_power_of_two(uint64_t value) {
  return value != 0u && (value & (value - 1u)) == 0u;
}

static bool cnet_callback_align_up(size_t value, size_t alignment, size_t *out_value) {
  const size_t remainder = value % alignment;
  const size_t padding = remainder == 0u ? 0u : alignment - remainder;
  if (value > SIZE_MAX - padding) return false;
  *out_value = value + padding;
  return true;
}

static bool cnet_callback_next_power_of_two(size_t value, size_t *out_value) {
  size_t power = 1u;
  while (power < value) {
    if (power > SIZE_MAX / 2u) return false;
    power *= 2u;
  }
  *out_value = power;
  return true;
}

static bool cnet_callback_storage_layout(const cnet_callback_workers_config *config,
                                         size_t *out_stride, size_t *out_storage_bytes) {
  size_t stride;
  size_t required;

  if (config->max_payload_bytes > SIZE_MAX - sizeof(cnet_callback_entry)) return false;
  if (!cnet_callback_align_up(sizeof(cnet_callback_entry) + config->max_payload_bytes,
                              alignof(cnet_callback_entry), &stride))
    return false;
  if (config->capacity_per_producer > SIZE_MAX - 1u) return false;
  if ((size_t)config->capacity_per_producer + 1u > SIZE_MAX / stride) return false;
  required = ((size_t)config->capacity_per_producer + 1u) * stride;
  if (!cnet_callback_next_power_of_two(required, out_storage_bytes)) return false;
  *out_stride = stride;
  return true;
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

static void cnet_callback_record_error(cnet_callback_workers_impl *impl, int status) {
  int expected = TURBO_OK;
  if (status == TURBO_OK) return;
  (void)atomic_compare_exchange_strong_explicit(&impl->first_error, &expected, status,
                                                memory_order_acq_rel, memory_order_acquire);
}

static bool cnet_callback_worker_running(const cnet_callback_worker *worker) {
  const cnet_callback_workers_impl *impl = worker->owner;
  size_t producer;

  for (producer = worker->index; producer < impl->producer_count; producer += impl->worker_count) {
    const cnet_callback_channel *channel = &impl->channels[producer];
    if (atomic_load_explicit(&channel->admission_open, memory_order_acquire) ||
        atomic_load_explicit(&channel->live_jobs, memory_order_acquire) != 0u ||
        atomic_load_explicit(&channel->publisher_entrants, memory_order_acquire) != 0u)
      return true;
  }
  return false;
}

static bool cnet_callback_worker_has_data(const cnet_callback_worker *worker) {
  const cnet_callback_workers_impl *impl = worker->owner;
  size_t producer;

  for (producer = worker->index; producer < impl->producer_count; producer += impl->worker_count)
    if (turbo_spsc_ring_read_available(&impl->channels[producer].ring) != 0u) return true;
  return false;
}

static void cnet_callback_worker_notify(cnet_callback_worker *worker) {
  if (atomic_exchange_explicit(&worker->wake_pending, true, memory_order_acq_rel)) return;
  turbo_mutex_lock(&worker->wake_lock);
  turbo_cond_signal(&worker->wake_cond);
  turbo_mutex_unlock(&worker->wake_lock);
}

static bool cnet_callback_worker_drain_channel(cnet_callback_worker *worker, size_t producer) {
  cnet_callback_workers_impl *impl = worker->owner;
  cnet_callback_channel *channel = &impl->channels[producer];
  size_t batch = 0u;
  bool progressed = false;

  while (batch < CNET_CALLBACK_MAX_BATCH_PER_CHANNEL) {
    size_t available = 0u;
    const cnet_callback_entry *entry =
        (const cnet_callback_entry *)turbo_spsc_ring_read_acquire(&channel->ring, &available);
    cnet_callback_view view;
    int status = TURBO_OK;

    if (entry == NULL) break;
    if (available < impl->entry_stride) {
      cnet_callback_record_error(impl, TURBO_EPROTO);
      break;
    }
    view = (cnet_callback_view){entry->kind,
                                entry->session,
                                entry->state,
                                entry->status,
                                entry->stage,
                                entry->size != 0u ? entry->payload : NULL,
                                entry->size};
    entry->invoke(entry->context, &view);
    if (entry->finish != NULL) entry->finish(entry->context, &view);
    if (entry->release != NULL)
      status = entry->release(entry->release_context, &view, entry->release_token);
    cnet_callback_record_error(impl, status);
    turbo_spsc_ring_read_release(&channel->ring, impl->entry_stride);
    atomic_fetch_sub_explicit(&channel->live_jobs, 1u, memory_order_release);
    ++batch;
    progressed = true;
  }
  return progressed;
}

static bool cnet_callback_worker_drain(cnet_callback_worker *worker) {
  cnet_callback_workers_impl *impl = worker->owner;
  size_t producer;
  bool progressed = false;

  for (producer = worker->index; producer < impl->producer_count; producer += impl->worker_count)
    if (cnet_callback_worker_drain_channel(worker, producer)) progressed = true;
  return progressed;
}

static void cnet_callback_worker_task(void *context) {
  cnet_callback_worker *worker = (cnet_callback_worker *)context;

  for (;;) {
    if (cnet_callback_worker_drain(worker)) continue;
    if (!cnet_callback_worker_running(worker)) break;

    turbo_mutex_lock(&worker->wake_lock);
    atomic_store_explicit(&worker->wake_pending, false, memory_order_release);
    if (!cnet_callback_worker_has_data(worker) && cnet_callback_worker_running(worker))
      turbo_cond_wait(&worker->wake_cond, &worker->wake_lock);
    turbo_mutex_unlock(&worker->wake_lock);
  }
  atomic_store_explicit(&worker->settled, true, memory_order_release);
}

static void cnet_callback_close_channels(cnet_callback_workers_impl *impl) {
  size_t producer;
  size_t worker;
  for (producer = 0u; producer < impl->producer_count; ++producer)
    atomic_store_explicit(&impl->channels[producer].admission_open, false, memory_order_release);
  for (worker = 0u; worker < impl->worker_count; ++worker)
    cnet_callback_worker_notify(&impl->workers[worker]);
}

static void cnet_callback_destroy_storage(cnet_callback_workers_impl *impl,
                                          size_t initialized_workers,
                                          size_t initialized_channels) {
  size_t index;
  if (impl == NULL) return;
  for (index = 0u; index < initialized_workers; ++index) {
    turbo_cond_destroy(&impl->workers[index].wake_cond);
    turbo_mutex_destroy(&impl->workers[index].wake_lock);
  }
  for (index = 0u; index < initialized_channels; ++index) free(impl->channels[index].storage);
  free(impl->workers);
  free(impl->channels);
}

int cnet_callback_workers_init(cnet_callback_workers *workers,
                               const cnet_callback_workers_config *config) {
  cnet_callback_workers_impl *impl;
  turbo_threadpool_config_t pool_config;
  size_t initialized_channels = 0u;
  size_t initialized_workers = 0u;
  size_t submitted_workers = 0u;
  size_t entry_stride;
  size_t storage_bytes;
  size_t index;
  int status = TURBO_OK;

  if (workers == NULL || config == NULL) return TURBO_EINVAL;
  if (workers->impl != NULL) return TURBO_EALREADY;
  if (config->worker_count == 0u || config->worker_count > INT_MAX ||
      config->producer_count == 0u || config->producer_count > UINT32_MAX ||
      !cnet_callback_power_of_two(config->capacity_per_producer) ||
      config->max_payload_bytes == 0u)
    return TURBO_EINVAL;
  if (!cnet_callback_storage_layout(config, &entry_stride, &storage_bytes)) return TURBO_ERANGE;
  if (config->producer_count > SIZE_MAX / sizeof(cnet_callback_channel) ||
      config->worker_count > SIZE_MAX / sizeof(cnet_callback_worker) ||
      config->producer_count > SIZE_MAX / storage_bytes)
    return TURBO_ERANGE;

  impl = (cnet_callback_workers_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return TURBO_ENOMEM;
  impl->channels =
      (cnet_callback_channel *)calloc(config->producer_count, sizeof(*impl->channels));
  impl->workers =
      (cnet_callback_worker *)calloc(config->worker_count, sizeof(*impl->workers));
  if (impl->channels == NULL || impl->workers == NULL) {
    cnet_callback_destroy_storage(impl, 0u, 0u);
    free(impl);
    return TURBO_ENOMEM;
  }
  impl->worker_count = config->worker_count;
  impl->producer_count = config->producer_count;
  impl->capacity_per_producer = config->capacity_per_producer;
  impl->max_payload_bytes = config->max_payload_bytes;
  impl->entry_stride = entry_stride;
  atomic_init(&impl->first_error, TURBO_OK);

  for (index = 0u; index < impl->producer_count; ++index) {
    cnet_callback_channel *channel = &impl->channels[index];
    channel->storage = (unsigned char *)calloc(storage_bytes, 1u);
    if (channel->storage == NULL) {
      status = TURBO_ENOMEM;
      break;
    }
    if (!turbo_spsc_ring_init(&channel->ring, channel->storage, storage_bytes)) {
      free(channel->storage);
      channel->storage = NULL;
      status = TURBO_EPROTO;
      break;
    }
    channel->worker = index % impl->worker_count;
    atomic_init(&channel->live_jobs, 0u);
    atomic_init(&channel->publisher_entrants, 0u);
    atomic_init(&channel->admission_open, true);
    ++initialized_channels;
  }
  for (index = 0u; status == TURBO_OK && index < impl->worker_count; ++index) {
    cnet_callback_worker *worker = &impl->workers[index];
    worker->owner = impl;
    worker->index = index;
    turbo_mutex_init(&worker->wake_lock);
    turbo_cond_init(&worker->wake_cond);
    atomic_init(&worker->wake_pending, false);
    atomic_init(&worker->settled, false);
    ++initialized_workers;
  }
  if (status != TURBO_OK) {
    cnet_callback_destroy_storage(impl, initialized_workers, initialized_channels);
    free(impl);
    return status;
  }

  pool_config = (turbo_threadpool_config_t){(int)impl->worker_count, impl->worker_count};
  impl->pool = turbo_threadpool_create_with_config(&pool_config);
  if (impl->pool == NULL) {
    cnet_callback_destroy_storage(impl, initialized_workers, initialized_channels);
    free(impl);
    return TURBO_ENOMEM;
  }
  for (index = 0u; index < impl->worker_count; ++index) {
    status = turbo_threadpool_try_submit(impl->pool, cnet_callback_worker_task,
                                         &impl->workers[index]);
    if (status != TURBO_OK) break;
    ++submitted_workers;
  }
  if (status != TURBO_OK) {
    cnet_callback_close_channels(impl);
    for (index = submitted_workers; index < impl->worker_count; ++index)
      atomic_store_explicit(&impl->workers[index].settled, true, memory_order_release);
    (void)turbo_threadpool_shutdown_with_policy(impl->pool, TURBO_THREADPOOL_SHUTDOWN_DRAIN);
    (void)turbo_threadpool_wait_status(impl->pool);
    turbo_threadpool_destroy(impl->pool);
    cnet_callback_destroy_storage(impl, initialized_workers, initialized_channels);
    free(impl);
    return status;
  }
  workers->impl = impl;
  return TURBO_OK;
}

bool cnet_callback_workers_get_config(const cnet_callback_workers *workers,
                                      cnet_callback_workers_config *out_config) {
  const cnet_callback_workers_impl *impl =
      workers != NULL ? (const cnet_callback_workers_impl *)workers->impl : NULL;
  if (impl == NULL || out_config == NULL) return false;
  *out_config = (cnet_callback_workers_config){impl->worker_count, impl->producer_count,
                                               impl->capacity_per_producer,
                                               impl->max_payload_bytes};
  return true;
}

int cnet_callback_workers_publish_from(cnet_callback_workers *workers, uint32_t producer,
                                       const cnet_callback_job *job) {
  cnet_callback_workers_impl *impl = cnet_callback_get(workers);
  cnet_callback_channel *channel;
  cnet_callback_entry *entry;

  if (impl == NULL || (size_t)producer >= impl->producer_count || job == NULL ||
      job->invoke == NULL || !cnet_callback_event_valid(&job->event))
    return TURBO_EINVAL;
  if (job->event.size > impl->max_payload_bytes) return TURBO_EMSGSIZE;
  channel = &impl->channels[producer];
  if (!atomic_load_explicit(&channel->admission_open, memory_order_acquire))
    return TURBO_ESHUTDOWN;

  atomic_fetch_add_explicit(&channel->publisher_entrants, 1u, memory_order_acq_rel);
  if (!atomic_load_explicit(&channel->admission_open, memory_order_acquire)) {
    atomic_fetch_sub_explicit(&channel->publisher_entrants, 1u, memory_order_release);
    cnet_callback_worker_notify(&impl->workers[channel->worker]);
    return TURBO_ESHUTDOWN;
  }
  if (atomic_load_explicit(&channel->live_jobs, memory_order_acquire) >=
      impl->capacity_per_producer) {
    atomic_fetch_sub_explicit(&channel->publisher_entrants, 1u, memory_order_release);
    return TURBO_ENOBUFS;
  }

  entry =
      (cnet_callback_entry *)turbo_spsc_ring_write_acquire(&channel->ring, impl->entry_stride);
  if (entry == NULL) {
    atomic_fetch_sub_explicit(&channel->publisher_entrants, 1u, memory_order_release);
    return TURBO_EPROTO;
  }
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
  entry->size = job->event.size;
  if (entry->size != 0u) memcpy(entry->payload, job->event.data, entry->size);
  atomic_fetch_add_explicit(&channel->live_jobs, 1u, memory_order_release);
  turbo_spsc_ring_write_release(&channel->ring, impl->entry_stride);
  atomic_fetch_sub_explicit(&channel->publisher_entrants, 1u, memory_order_release);
  cnet_callback_worker_notify(&impl->workers[channel->worker]);
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
    cnet_callback_close_channels(impl);
  }

  for (;;) {
    bool settled = true;
    for (index = 0u; index < impl->worker_count; ++index)
      if (!atomic_load_explicit(&impl->workers[index].settled, memory_order_acquire)) {
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
  for (index = 0u; index < impl->producer_count; ++index)
    if (atomic_load_explicit(&impl->channels[index].live_jobs, memory_order_acquire) != 0u ||
        atomic_load_explicit(&impl->channels[index].publisher_entrants, memory_order_acquire) != 0u)
      return TURBO_EBUSY;

  turbo_threadpool_destroy(impl->pool);
  cnet_callback_destroy_storage(impl, impl->worker_count, impl->producer_count);
  free(impl);
  workers->impl = NULL;
  return TURBO_OK;
}
