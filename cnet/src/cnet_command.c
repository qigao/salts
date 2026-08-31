#include "cnet_command.h"

#include <turbo/disruptor.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct cnet_command_entry {
  cnet_command_kind kind;
  cnet_session_handle connection;
  size_t size;
  size_t argument;
  unsigned char payload[];
} cnet_command_entry;

typedef struct cnet_command_queue_impl {
  disruptor_t *ring;
  size_t max_payload_bytes;
  atomic_bool admission_open;
  atomic_bool close_complete;
  atomic_size_t publisher_entrants;
  atomic_size_t queued;
  atomic_size_t peak_queued;
  atomic_size_t queued_bytes;
  atomic_size_t peak_queued_bytes;
  atomic_uint_fast64_t rejected_commands;
  atomic_uint_fast64_t rejected_bytes;
  uint64_t *borrowed_sequences;
  size_t borrowed_capacity;
  size_t borrowed_count;
} cnet_command_queue_impl;

static cnet_command_queue_impl *cnet_command_impl(cnet_command_queue *queue) {
  return queue != NULL ? (cnet_command_queue_impl *)queue->impl : NULL;
}

static const cnet_command_queue_impl *cnet_command_const_impl(const cnet_command_queue *queue) {
  return queue != NULL ? (const cnet_command_queue_impl *)queue->impl : NULL;
}

static void cnet_command_update_peak(atomic_size_t *peak, size_t value) {
  size_t observed = atomic_load_explicit(peak, memory_order_relaxed);
  while (observed < value &&
         !atomic_compare_exchange_weak_explicit(peak, &observed, value, memory_order_relaxed,
                                                memory_order_relaxed)) {
  }
}

static void cnet_command_saturating_add(atomic_uint_fast64_t *counter, uint64_t value) {
  uint_fast64_t observed = atomic_load_explicit(counter, memory_order_relaxed);
  for (;;) {
    const uint_fast64_t next = observed > UINT64_MAX - value ? UINT64_MAX : observed + value;
    if (atomic_compare_exchange_weak_explicit(counter, &observed, next, memory_order_relaxed,
                                              memory_order_relaxed))
      return;
  }
}

static void cnet_command_record_rejection(cnet_command_queue_impl *impl, size_t bytes) {
  cnet_command_saturating_add(&impl->rejected_commands, 1u);
  cnet_command_saturating_add(&impl->rejected_bytes, (uint64_t)bytes);
}

static bool cnet_is_power_of_two(uint64_t value) {
  return value != 0u && (value & (value - 1u)) == 0u;
}

static bool cnet_command_valid(const cnet_command *command) {
  if (command == NULL || command->kind <= CNET_COMMAND_NONE || command->kind > CNET_COMMAND_STOP)
    return false;

  if (command->kind == CNET_COMMAND_STOP)
    return !cnet_session_handle_valid(command->connection) && command->data == NULL &&
           command->size == 0u;

  if (!cnet_session_handle_valid(command->connection)) return false;
  if (command->kind == CNET_COMMAND_CONNECT || command->kind == CNET_COMMAND_SEND)
    return command->data != NULL && command->size != 0u;
  return command->data == NULL && command->size == 0u;
}

int cnet_command_queue_init(cnet_command_queue *queue, const cnet_command_queue_config *config) {
  cnet_command_queue_impl *impl;
  disruptor_config_t ring_config;
  size_t entry_size;

  if (queue == NULL) return TURBO_EINVAL;
  if (queue->impl != NULL) return TURBO_EALREADY;
  if (config == NULL || !cnet_is_power_of_two(config->capacity) || config->max_payload_bytes == 0u)
    return TURBO_EINVAL;
  if (config->max_payload_bytes > SIZE_MAX - offsetof(cnet_command_entry, payload))
    return TURBO_ERANGE;
  entry_size = offsetof(cnet_command_entry, payload) + config->max_payload_bytes;
  if (config->capacity > SIZE_MAX / entry_size) return TURBO_ERANGE;

  impl = (cnet_command_queue_impl *)calloc(1u, sizeof(*impl));
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
  impl->borrowed_capacity = (size_t)config->capacity;
  impl->max_payload_bytes = config->max_payload_bytes;
  atomic_init(&impl->admission_open, true);
  atomic_init(&impl->close_complete, false);
  atomic_init(&impl->publisher_entrants, 0u);
  atomic_init(&impl->queued, 0u);
  atomic_init(&impl->peak_queued, 0u);
  atomic_init(&impl->queued_bytes, 0u);
  atomic_init(&impl->peak_queued_bytes, 0u);
  atomic_init(&impl->rejected_commands, 0u);
  atomic_init(&impl->rejected_bytes, 0u);
  queue->impl = impl;
  return TURBO_OK;
}

int cnet_command_queue_publish(cnet_command_queue *queue, const cnet_command *command) {
  cnet_command_queue_impl *impl = cnet_command_impl(queue);
  disruptor_cursor_t cursor = {0};
  cnet_command_entry *entry;

  if (impl == NULL || !cnet_command_valid(command)) return TURBO_EINVAL;
  if (command->size > impl->max_payload_bytes) {
    cnet_command_record_rejection(impl, command->size);
    return TURBO_EMSGSIZE;
  }
  if (!atomic_load_explicit(&impl->admission_open, memory_order_acquire)) {
    cnet_command_record_rejection(impl, command->size);
    return TURBO_ESHUTDOWN;
  }

  atomic_fetch_add_explicit(&impl->publisher_entrants, 1u, memory_order_acq_rel);
  if (!atomic_load_explicit(&impl->admission_open, memory_order_acquire)) {
    cnet_command_record_rejection(impl, command->size);
    atomic_fetch_sub_explicit(&impl->publisher_entrants, 1u, memory_order_release);
    return TURBO_ESHUTDOWN;
  }
  if (!disruptor_publisher_try_claim(impl->ring, &cursor)) {
    cnet_command_record_rejection(impl, command->size);
    atomic_fetch_sub_explicit(&impl->publisher_entrants, 1u, memory_order_release);
    return TURBO_ENOBUFS;
  }

  entry = (cnet_command_entry *)disruptor_acquire_entry(impl->ring, &cursor);
  entry->kind = command->kind;
  entry->connection = command->connection;
  entry->size = command->size;
  entry->argument = command->argument;
  if (command->size != 0u) memcpy(entry->payload, command->data, command->size);
  {
    const size_t live = atomic_fetch_add_explicit(&impl->queued, 1u, memory_order_release) + 1u;
    const size_t bytes =
        atomic_fetch_add_explicit(&impl->queued_bytes, command->size, memory_order_release) +
        command->size;
    cnet_command_update_peak(&impl->peak_queued, live);
    cnet_command_update_peak(&impl->peak_queued_bytes, bytes);
  }
  (void)disruptor_publisher_publish(impl->ring, &cursor);
  atomic_fetch_sub_explicit(&impl->publisher_entrants, 1u, memory_order_release);
  return TURBO_OK;
}

int cnet_command_queue_take(cnet_command_queue *queue, cnet_command_view *out_view) {
  cnet_command_queue_impl *impl = cnet_command_impl(queue);
  disruptor_cursor_t cursor = {0};
  const cnet_command_entry *entry;

  if (out_view == NULL) return TURBO_EINVAL;
  memset(out_view, 0, sizeof(*out_view));
  if (impl == NULL) return TURBO_EINVAL;
  if (!disruptor_worker_try_claim(impl->ring, &cursor)) {
    if (atomic_load_explicit(&impl->close_complete, memory_order_acquire) &&
        atomic_load_explicit(&impl->queued, memory_order_acquire) == 0u)
      return TURBO_EOF;
    return TURBO_ETIMEDOUT;
  }

  entry = (const cnet_command_entry *)disruptor_show_entry(impl->ring, &cursor);
  out_view->kind = entry->kind;
  out_view->connection = entry->connection;
  out_view->data = entry->size != 0u ? entry->payload : NULL;
  out_view->size = entry->size;
  out_view->argument = entry->argument;
  out_view->_sequence = cursor.sequence;
  impl->borrowed_sequences[(size_t)((cursor.sequence - 1u) & (impl->borrowed_capacity - 1u))] =
      cursor.sequence;
  ++impl->borrowed_count;
  return TURBO_OK;
}

int cnet_command_queue_release(cnet_command_queue *queue, cnet_command_view *view) {
  cnet_command_queue_impl *impl = cnet_command_impl(queue);
  disruptor_cursor_t cursor;
  size_t slot;

  if (impl == NULL || view == NULL || view->_sequence == 0u) return TURBO_EINVAL;
  slot = (size_t)((view->_sequence - 1u) & (impl->borrowed_capacity - 1u));
  if (impl->borrowed_sequences[slot] != view->_sequence) return TURBO_EINVAL;
  cursor.sequence = view->_sequence;
  disruptor_worker_release_entry(impl->ring, &cursor);
  atomic_fetch_sub_explicit(&impl->queued, 1u, memory_order_release);
  atomic_fetch_sub_explicit(&impl->queued_bytes, view->size, memory_order_release);
  impl->borrowed_sequences[slot] = 0u;
  --impl->borrowed_count;
  memset(view, 0, sizeof(*view));
  return TURBO_OK;
}

int cnet_command_queue_close(cnet_command_queue *queue) {
  cnet_command_queue_impl *impl = cnet_command_impl(queue);
  bool was_open;

  if (impl == NULL) return TURBO_EINVAL;
  was_open = atomic_exchange_explicit(&impl->admission_open, false, memory_order_acq_rel);
  if (atomic_load_explicit(&impl->publisher_entrants, memory_order_acquire) != 0u)
    return TURBO_EBUSY;
  if (atomic_exchange_explicit(&impl->close_complete, true, memory_order_acq_rel))
    return TURBO_EALREADY;
  (void)was_open;
  return TURBO_OK;
}

bool cnet_command_queue_get_stats(const cnet_command_queue *queue,
                                  cnet_command_queue_stats *out_stats) {
  const cnet_command_queue_impl *impl = cnet_command_const_impl(queue);
  if (impl == NULL || out_stats == NULL) return false;
  *out_stats = (cnet_command_queue_stats){
      atomic_load_explicit(&impl->queued, memory_order_acquire),
      atomic_load_explicit(&impl->peak_queued, memory_order_relaxed),
      atomic_load_explicit(&impl->queued_bytes, memory_order_acquire),
      atomic_load_explicit(&impl->peak_queued_bytes, memory_order_relaxed),
      (uint64_t)atomic_load_explicit(&impl->rejected_commands, memory_order_relaxed),
      (uint64_t)atomic_load_explicit(&impl->rejected_bytes, memory_order_relaxed),
      atomic_load_explicit(&impl->admission_open, memory_order_acquire)};
  return true;
}

int cnet_command_queue_destroy(cnet_command_queue *queue) {
  cnet_command_queue_impl *impl = cnet_command_impl(queue);

  if (queue == NULL) return TURBO_EINVAL;
  if (impl == NULL) return TURBO_OK;
  if (!atomic_load_explicit(&impl->close_complete, memory_order_acquire) ||
      atomic_load_explicit(&impl->publisher_entrants, memory_order_acquire) != 0u ||
      atomic_load_explicit(&impl->queued, memory_order_acquire) != 0u || impl->borrowed_count != 0u)
    return TURBO_EBUSY;
  disruptor_destroy(impl->ring);
  free(impl->borrowed_sequences);
  free(impl);
  queue->impl = NULL;
  return TURBO_OK;
}
