#include "cnet_event.h"

#include <turbo/disruptor.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct cnet_event_entry {
  cnet_event_kind kind;
  cnet_session_handle session;
  cnet_event_state state;
  int status;
  cnet_session_stage stage;
  size_t size;
  unsigned char payload[];
} cnet_event_entry;

typedef struct cnet_event_queue_impl {
  disruptor_t *ring;
  size_t data_capacity;
  size_t max_payload_bytes;
  atomic_size_t live_events;
  atomic_size_t live_data_events;
  atomic_size_t publisher_entrants;
  atomic_bool admission_open;
  atomic_bool close_complete;
  uint64_t *borrowed_sequences;
  size_t borrowed_capacity;
  size_t borrowed_count;
} cnet_event_queue_impl;

static cnet_event_queue_impl *cnet_event_impl(cnet_event_queue *queue) {
  return queue != NULL ? (cnet_event_queue_impl *)queue->impl : NULL;
}

static const cnet_event_queue_impl *cnet_event_const_impl(const cnet_event_queue *queue) {
  return queue != NULL ? (const cnet_event_queue_impl *)queue->impl : NULL;
}

static bool cnet_event_power_of_two(uint64_t value) {
  return value != 0u && (value & (value - 1u)) == 0u;
}

static bool cnet_event_valid(const cnet_event *event) {
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

static bool cnet_event_reserve_data(cnet_event_queue_impl *impl) {
  size_t observed = atomic_load_explicit(&impl->live_data_events, memory_order_relaxed);
  while (observed < impl->data_capacity) {
    if (atomic_compare_exchange_weak_explicit(&impl->live_data_events, &observed, observed + 1u,
                                              memory_order_acq_rel, memory_order_relaxed))
      return true;
  }
  return false;
}

int cnet_event_queue_init(cnet_event_queue *queue, const cnet_event_queue_config *config) {
  cnet_event_queue_impl *impl;
  disruptor_config_t ring_config;
  size_t entry_size;

  if (queue == NULL) return TURBO_EINVAL;
  if (queue->impl != NULL) return TURBO_EALREADY;
  if (config == NULL || !cnet_event_power_of_two(config->capacity) || config->data_capacity == 0u ||
      config->data_capacity >= config->capacity || config->max_payload_bytes == 0u)
    return TURBO_EINVAL;
  if (config->max_payload_bytes > SIZE_MAX - offsetof(cnet_event_entry, payload))
    return TURBO_ERANGE;
  entry_size = offsetof(cnet_event_entry, payload) + config->max_payload_bytes;
  if (config->capacity > SIZE_MAX / entry_size) return TURBO_ERANGE;

  impl = (cnet_event_queue_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return TURBO_ENOMEM;
  ring_config.entry_size = entry_size;
  ring_config.capacity = config->capacity;
  ring_config.consumer_capacity = 1u;
  ring_config.mode = DISRUPTOR_MODE_WORKER_POOL;
  impl->ring = disruptor_create(&ring_config);
  if (impl->ring == NULL) {
    free(impl);
    return TURBO_ENOMEM;
  }
  impl->borrowed_sequences = (uint64_t *)calloc((size_t)config->capacity, sizeof(uint64_t));
  if (impl->borrowed_sequences == NULL) {
    disruptor_destroy(impl->ring);
    free(impl);
    return TURBO_ENOMEM;
  }
  impl->data_capacity = config->data_capacity;
  impl->max_payload_bytes = config->max_payload_bytes;
  impl->borrowed_capacity = (size_t)config->capacity;
  atomic_init(&impl->live_events, 0u);
  atomic_init(&impl->live_data_events, 0u);
  atomic_init(&impl->publisher_entrants, 0u);
  atomic_init(&impl->admission_open, true);
  atomic_init(&impl->close_complete, false);
  queue->impl = impl;
  return TURBO_OK;
}

bool cnet_event_queue_get_config(const cnet_event_queue *queue,
                                 cnet_event_queue_config *out_config) {
  const cnet_event_queue_impl *impl = cnet_event_const_impl(queue);
  if (impl == NULL || out_config == NULL) return false;
  *out_config = (cnet_event_queue_config){impl->borrowed_capacity, impl->data_capacity,
                                          impl->max_payload_bytes};
  return true;
}

int cnet_event_queue_publish(cnet_event_queue *queue, const cnet_event *event) {
  cnet_event_queue_impl *impl = cnet_event_impl(queue);
  disruptor_cursor_t cursor = {0};
  cnet_event_entry *entry;
  const bool data_event = event != NULL && event->kind == CNET_EVENT_RECEIVE;

  if (impl == NULL || !cnet_event_valid(event)) return TURBO_EINVAL;
  if (event->size > impl->max_payload_bytes) return TURBO_EMSGSIZE;
  if (!atomic_load_explicit(&impl->admission_open, memory_order_acquire)) return TURBO_ESHUTDOWN;

  atomic_fetch_add_explicit(&impl->publisher_entrants, 1u, memory_order_acq_rel);
  if (!atomic_load_explicit(&impl->admission_open, memory_order_acquire)) {
    atomic_fetch_sub_explicit(&impl->publisher_entrants, 1u, memory_order_release);
    return TURBO_ESHUTDOWN;
  }
  if (data_event && !cnet_event_reserve_data(impl)) {
    atomic_fetch_sub_explicit(&impl->publisher_entrants, 1u, memory_order_release);
    return TURBO_ENOBUFS;
  }
  if (!disruptor_publisher_try_claim(impl->ring, &cursor)) {
    if (data_event) atomic_fetch_sub_explicit(&impl->live_data_events, 1u, memory_order_release);
    atomic_fetch_sub_explicit(&impl->publisher_entrants, 1u, memory_order_release);
    return TURBO_ENOBUFS;
  }

  entry = (cnet_event_entry *)disruptor_acquire_entry(impl->ring, &cursor);
  entry->kind = event->kind;
  entry->session = event->session;
  entry->state = event->state;
  entry->status = event->status;
  entry->stage = event->stage;
  entry->size = event->size;
  if (event->size != 0u) memcpy(entry->payload, event->data, event->size);
  atomic_fetch_add_explicit(&impl->live_events, 1u, memory_order_release);
  (void)disruptor_publisher_publish(impl->ring, &cursor);
  atomic_fetch_sub_explicit(&impl->publisher_entrants, 1u, memory_order_release);
  return TURBO_OK;
}

int cnet_event_queue_take(cnet_event_queue *queue, cnet_event_view *out_view) {
  cnet_event_queue_impl *impl = cnet_event_impl(queue);
  disruptor_cursor_t cursor = {0};
  const cnet_event_entry *entry;

  if (out_view == NULL) return TURBO_EINVAL;
  memset(out_view, 0, sizeof(*out_view));
  if (impl == NULL) return TURBO_EINVAL;
  if (!disruptor_worker_try_claim(impl->ring, &cursor)) {
    if (atomic_load_explicit(&impl->close_complete, memory_order_acquire) &&
        atomic_load_explicit(&impl->live_events, memory_order_acquire) == 0u)
      return TURBO_EOF;
    return TURBO_ETIMEDOUT;
  }

  entry = (const cnet_event_entry *)disruptor_show_entry(impl->ring, &cursor);
  out_view->kind = entry->kind;
  out_view->session = entry->session;
  out_view->state = entry->state;
  out_view->status = entry->status;
  out_view->stage = entry->stage;
  out_view->data = entry->size != 0u ? entry->payload : NULL;
  out_view->size = entry->size;
  out_view->_sequence = cursor.sequence;
  impl->borrowed_sequences[(size_t)((cursor.sequence - 1u) & (impl->borrowed_capacity - 1u))] =
      cursor.sequence;
  ++impl->borrowed_count;
  return TURBO_OK;
}

int cnet_event_queue_release(cnet_event_queue *queue, cnet_event_view *view) {
  cnet_event_queue_impl *impl = cnet_event_impl(queue);
  disruptor_cursor_t cursor;
  size_t slot;

  if (impl == NULL || view == NULL || view->_sequence == 0u) return TURBO_EINVAL;
  slot = (size_t)((view->_sequence - 1u) & (impl->borrowed_capacity - 1u));
  if (impl->borrowed_sequences[slot] != view->_sequence) return TURBO_EINVAL;
  cursor.sequence = view->_sequence;
  disruptor_worker_release_entry(impl->ring, &cursor);
  atomic_fetch_sub_explicit(&impl->live_events, 1u, memory_order_release);
  if (view->kind == CNET_EVENT_RECEIVE)
    atomic_fetch_sub_explicit(&impl->live_data_events, 1u, memory_order_release);
  impl->borrowed_sequences[slot] = 0u;
  --impl->borrowed_count;
  memset(view, 0, sizeof(*view));
  return TURBO_OK;
}

int cnet_event_queue_close(cnet_event_queue *queue) {
  cnet_event_queue_impl *impl = cnet_event_impl(queue);

  if (impl == NULL) return TURBO_EINVAL;
  (void)atomic_exchange_explicit(&impl->admission_open, false, memory_order_acq_rel);
  if (atomic_load_explicit(&impl->publisher_entrants, memory_order_acquire) != 0u)
    return TURBO_EBUSY;
  if (atomic_exchange_explicit(&impl->close_complete, true, memory_order_acq_rel))
    return TURBO_EALREADY;
  return TURBO_OK;
}

int cnet_event_queue_destroy(cnet_event_queue *queue) {
  cnet_event_queue_impl *impl = cnet_event_impl(queue);

  if (queue == NULL) return TURBO_EINVAL;
  if (impl == NULL) return TURBO_OK;
  if (!atomic_load_explicit(&impl->close_complete, memory_order_acquire) ||
      atomic_load_explicit(&impl->publisher_entrants, memory_order_acquire) != 0u ||
      atomic_load_explicit(&impl->live_events, memory_order_acquire) != 0u ||
      impl->borrowed_count != 0u)
    return TURBO_EBUSY;
  disruptor_destroy(impl->ring);
  free(impl->borrowed_sequences);
  free(impl);
  queue->impl = NULL;
  return TURBO_OK;
}
