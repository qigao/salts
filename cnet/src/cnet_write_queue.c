#include "cnet_write_queue.h"

#include <salts/error_codes.h>
#include <salts_buffer.h>

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CNET_WRITE_SLOT_NONE UINT32_MAX

typedef enum cnet_write_entry_state {
  CNET_WRITE_ENTRY_FREE = 0,
  CNET_WRITE_ENTRY_QUEUED
} cnet_write_entry_state;

typedef enum cnet_write_payload_kind {
  CNET_WRITE_PAYLOAD_COPIED = 0,
  CNET_WRITE_PAYLOAD_RETAINED
} cnet_write_payload_kind;

typedef struct cnet_write_entry {
  cnet_session_handle connection;
  mem_buffer_t *payload;
  size_t size;
  size_t offset;
  size_t copied_bytes;
  uint32_t generation;
  uint32_t next;
  cnet_write_entry_state state;
  cnet_write_payload_kind payload_kind;
  bool close_after_send;
} cnet_write_entry;

typedef struct cnet_write_queue_impl {
  cnet_write_entry *entries;
  uint32_t *free_slots;
  uint32_t *heads;
  uint32_t *tails;
  size_t *counts;
  mem_pool_t payload_pool;
  size_t connection_capacity;
  size_t capacity;
  size_t max_payload_bytes;
  size_t payload_capacity_bytes;
  size_t free_count;
  size_t live_writes;
  size_t peak_writes;
  size_t copied_bytes;
  size_t peak_copied_bytes;
  bool admission_open;
} cnet_write_queue_impl;

static cnet_write_queue_impl *cnet_write_impl(cnet_write_queue *queue) {
  return queue != NULL ? (cnet_write_queue_impl *)queue->impl : NULL;
}

static const cnet_write_queue_impl *cnet_write_const_impl(const cnet_write_queue *queue) {
  return queue != NULL ? (const cnet_write_queue_impl *)queue->impl : NULL;
}

static uint32_t cnet_write_next_generation(uint32_t generation) {
  return generation == UINT32_MAX ? 1u : generation + 1u;
}

static uint64_t cnet_write_token(size_t slot, uint32_t generation) {
  return ((uint64_t)generation << 32u) | ((uint64_t)slot + 1u);
}

static size_t cnet_write_connection_index(const cnet_write_queue_impl *impl,
                                          cnet_session_handle connection) {
  if (!cnet_session_handle_valid(connection) || (size_t)connection.slot > impl->connection_capacity)
    return SIZE_MAX;
  return (size_t)connection.slot - 1u;
}

static bool cnet_write_entry_matches(const cnet_write_entry *entry,
                                     cnet_session_handle connection) {
  return entry->state == CNET_WRITE_ENTRY_QUEUED &&
         entry->connection.slot == connection.slot &&
         entry->connection.generation == connection.generation;
}

static int cnet_write_validate_payload(cnet_write_queue_impl *impl, cnet_session_handle connection,
                                       size_t size) {
  if (cnet_write_connection_index(impl, connection) == SIZE_MAX || size == 0u) return SALTS_EINVAL;
  if (size > impl->max_payload_bytes) return SALTS_EMSGSIZE;
  if (!impl->admission_open) return SALTS_ESHUTDOWN;
  if (impl->free_count == 0u) return SALTS_ENOBUFS;
  return SALTS_OK;
}

static int cnet_write_enqueue_owned(cnet_write_queue_impl *impl, cnet_session_handle connection,
                                    mem_buffer_t *payload, size_t size, size_t copied_bytes,
                                    cnet_write_payload_kind payload_kind, bool close_after_send,
                                    cnet_write_handle *out_handle) {
  const size_t connection_index = cnet_write_connection_index(impl, connection);
  cnet_write_entry *entry;
  uint32_t slot;
  uint32_t tail;

  if (out_handle == NULL) return SALTS_EINVAL;
  *out_handle = (cnet_write_handle){0};
  if (connection_index == SIZE_MAX || payload == NULL || size == 0u) return SALTS_EINVAL;

  if (impl->heads[connection_index] != CNET_WRITE_SLOT_NONE) {
    const cnet_write_entry *head = &impl->entries[impl->heads[connection_index]];
    if (!cnet_write_entry_matches(head, connection)) return SALTS_EBUSY;
  }

  slot = impl->free_slots[--impl->free_count];
  entry = &impl->entries[slot];
  if (entry->state != CNET_WRITE_ENTRY_FREE) {
    ++impl->free_count;
    return SALTS_EPROTO;
  }

  entry->generation = cnet_write_next_generation(entry->generation);
  entry->connection = connection;
  entry->payload = payload;
  entry->size = size;
  entry->offset = 0u;
  entry->copied_bytes = copied_bytes;
  entry->next = CNET_WRITE_SLOT_NONE;
  entry->payload_kind = payload_kind;
  entry->close_after_send = close_after_send;
  entry->state = CNET_WRITE_ENTRY_QUEUED;

  tail = impl->tails[connection_index];
  if (tail == CNET_WRITE_SLOT_NONE) {
    impl->heads[connection_index] = slot;
  } else {
    if (tail >= impl->capacity || impl->entries[tail].state != CNET_WRITE_ENTRY_QUEUED) {
      entry->state = CNET_WRITE_ENTRY_FREE;
      entry->payload = NULL;
      impl->free_slots[impl->free_count++] = slot;
      return SALTS_EPROTO;
    }
    impl->entries[tail].next = slot;
  }
  impl->tails[connection_index] = slot;
  ++impl->counts[connection_index];
  ++impl->live_writes;
  impl->copied_bytes += copied_bytes;
  if (impl->peak_writes < impl->live_writes) impl->peak_writes = impl->live_writes;
  if (impl->peak_copied_bytes < impl->copied_bytes) impl->peak_copied_bytes = impl->copied_bytes;

  *out_handle = (cnet_write_handle){slot + 1u, entry->generation};
  return SALTS_OK;
}

bool cnet_write_handle_valid(cnet_write_handle handle) {
  return handle.slot != 0u && handle.generation != 0u;
}

int cnet_write_queue_init(cnet_write_queue *queue, const cnet_write_queue_config *config) {
  cnet_write_queue_impl *impl;
  size_t index;

  if (queue == NULL || config == NULL) return SALTS_EINVAL;
  if (queue->impl != NULL) return SALTS_EALREADY;
  if (config->connection_capacity == 0u || config->capacity == 0u ||
      config->max_payload_bytes == 0u || config->payload_capacity_bytes < config->max_payload_bytes)
    return SALTS_EINVAL;
  if (config->connection_capacity > UINT32_MAX || config->capacity > UINT32_MAX ||
      config->capacity > SIZE_MAX / sizeof(cnet_write_entry) ||
      config->capacity > SIZE_MAX / sizeof(uint32_t) ||
      config->connection_capacity > SIZE_MAX / sizeof(uint32_t) ||
      config->connection_capacity > SIZE_MAX / sizeof(size_t))
    return SALTS_ERANGE;

  impl = (cnet_write_queue_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return SALTS_ENOMEM;
  impl->entries = (cnet_write_entry *)calloc(config->capacity, sizeof(*impl->entries));
  impl->free_slots = (uint32_t *)calloc(config->capacity, sizeof(*impl->free_slots));
  impl->heads = (uint32_t *)malloc(config->connection_capacity * sizeof(*impl->heads));
  impl->tails = (uint32_t *)malloc(config->connection_capacity * sizeof(*impl->tails));
  impl->counts = (size_t *)calloc(config->connection_capacity, sizeof(*impl->counts));
  if (impl->entries == NULL || impl->free_slots == NULL || impl->heads == NULL ||
      impl->tails == NULL || impl->counts == NULL || mem_init(&impl->payload_pool, 0u) != 0) {
    free(impl->counts);
    free(impl->tails);
    free(impl->heads);
    free(impl->free_slots);
    free(impl->entries);
    free(impl);
    return SALTS_ENOMEM;
  }

  impl->connection_capacity = config->connection_capacity;
  impl->capacity = config->capacity;
  impl->max_payload_bytes = config->max_payload_bytes;
  impl->payload_capacity_bytes = config->payload_capacity_bytes;
  impl->free_count = config->capacity;
  impl->admission_open = true;
  for (index = 0u; index < config->capacity; ++index)
    impl->free_slots[index] = (uint32_t)(config->capacity - index - 1u);
  for (index = 0u; index < config->connection_capacity; ++index) {
    impl->heads[index] = CNET_WRITE_SLOT_NONE;
    impl->tails[index] = CNET_WRITE_SLOT_NONE;
  }
  queue->impl = impl;
  return SALTS_OK;
}

int cnet_write_queue_enqueue_copy(cnet_write_queue *queue, cnet_session_handle connection,
                                  const void *data, size_t size, bool close_after_send,
                                  cnet_write_handle *out_handle) {
  cnet_write_queue_impl *impl = cnet_write_impl(queue);
  mem_buffer_t *payload;
  int status;

  if (out_handle == NULL) return SALTS_EINVAL;
  *out_handle = (cnet_write_handle){0};
  if (impl == NULL || data == NULL) return SALTS_EINVAL;
  status = cnet_write_validate_payload(impl, connection, size);
  if (status != SALTS_OK) return status;
  if (impl->copied_bytes > impl->payload_capacity_bytes ||
      size > impl->payload_capacity_bytes - impl->copied_bytes)
    return SALTS_ENOBUFS;

  payload = mem_get_buffer(&impl->payload_pool, size);
  if (payload == NULL) return SALTS_ENOMEM;
  memcpy(mem_buffer_data(payload), data, size);
  mem_set_used(payload, size);
  status = cnet_write_enqueue_owned(impl, connection, payload, size, size,
                                    CNET_WRITE_PAYLOAD_COPIED, close_after_send, out_handle);
  if (status != SALTS_OK) mem_buffer_release(payload);
  return status;
}

int cnet_write_queue_enqueuev_copy(cnet_write_queue *queue, cnet_session_handle connection,
                                   const cnet_const_buffer *segments, size_t segment_count,
                                   bool close_after_send, cnet_write_handle *out_handle) {
  cnet_write_queue_impl *impl = cnet_write_impl(queue);
  mem_buffer_t *payload;
  size_t total = 0u;
  size_t offset = 0u;
  int status;

  if (out_handle == NULL) return SALTS_EINVAL;
  *out_handle = (cnet_write_handle){0};
  if (impl == NULL || segments == NULL || segment_count == 0u) return SALTS_EINVAL;
  for (size_t index = 0u; index < segment_count; ++index) {
    if (segments[index].data == NULL || segments[index].size == 0u) return SALTS_EINVAL;
    if (segments[index].size > SIZE_MAX - total) return SALTS_EMSGSIZE;
    total += segments[index].size;
  }
  status = cnet_write_validate_payload(impl, connection, total);
  if (status != SALTS_OK) return status;
  if (impl->copied_bytes > impl->payload_capacity_bytes ||
      total > impl->payload_capacity_bytes - impl->copied_bytes)
    return SALTS_ENOBUFS;

  payload = mem_get_buffer(&impl->payload_pool, total);
  if (payload == NULL) return SALTS_ENOMEM;
  for (size_t index = 0u; index < segment_count; ++index) {
    memcpy(mem_buffer_data(payload) + offset, segments[index].data, segments[index].size);
    offset += segments[index].size;
  }
  mem_set_used(payload, total);
  status = cnet_write_enqueue_owned(impl, connection, payload, total, total,
                                    CNET_WRITE_PAYLOAD_COPIED, close_after_send, out_handle);
  if (status != SALTS_OK) mem_buffer_release(payload);
  return status;
}

int cnet_write_queue_enqueue_buffer(cnet_write_queue *queue, cnet_session_handle connection,
                                    mem_buffer_t *buffer, bool close_after_send,
                                    cnet_write_handle *out_handle) {
  cnet_write_queue_impl *impl = cnet_write_impl(queue);
  mem_buffer_t *payload;
  size_t size;
  int status;

  if (out_handle == NULL) return SALTS_EINVAL;
  *out_handle = (cnet_write_handle){0};
  if (impl == NULL || buffer == NULL) return SALTS_EINVAL;
  size = mem_buffer_used(buffer);
  if (size == 0u || mem_buffer_const_data(buffer) == NULL) return SALTS_EINVAL;
  status = cnet_write_validate_payload(impl, connection, size);
  if (status != SALTS_OK) return status;

  payload = mem_buffer_retain(buffer);
  if (payload == NULL) return SALTS_EINVAL;
  status = cnet_write_enqueue_owned(impl, connection, payload, size, 0u,
                                    CNET_WRITE_PAYLOAD_RETAINED, close_after_send, out_handle);
  if (status != SALTS_OK) mem_buffer_release(payload);
  return status;
}

int cnet_write_queue_peek(cnet_write_queue *queue, cnet_session_handle connection,
                          cnet_write_view *out_view) {
  cnet_write_queue_impl *impl = cnet_write_impl(queue);
  cnet_write_entry *entry;
  size_t connection_index;
  uint32_t slot;

  if (out_view == NULL) return SALTS_EINVAL;
  *out_view = (cnet_write_view){0};
  if (impl == NULL) return SALTS_EINVAL;
  connection_index = cnet_write_connection_index(impl, connection);
  if (connection_index == SIZE_MAX) return SALTS_EINVAL;
  slot = impl->heads[connection_index];
  if (slot == CNET_WRITE_SLOT_NONE) return SALTS_ETIMEDOUT;
  if ((size_t)slot >= impl->capacity) return SALTS_EPROTO;
  entry = &impl->entries[slot];
  if (!cnet_write_entry_matches(entry, connection) || entry->offset > entry->size)
    return SALTS_EPROTO;

  *out_view = (cnet_write_view){
      .handle = {slot + 1u, entry->generation},
      .connection = entry->connection,
      .data = (const unsigned char *)mem_buffer_const_data(entry->payload) + entry->offset,
      .size = entry->size,
      .offset = entry->offset,
      .remaining = entry->size - entry->offset,
      .close_after_send = entry->close_after_send,
      ._token = cnet_write_token(slot, entry->generation)};
  return SALTS_OK;
}

int cnet_write_queue_advance(cnet_write_queue *queue, cnet_write_view *view, size_t bytes) {
  cnet_write_queue_impl *impl = cnet_write_impl(queue);
  cnet_write_entry *entry;
  size_t connection_index;
  uint32_t slot;

  if (impl == NULL || view == NULL || !cnet_write_handle_valid(view->handle) || bytes == 0u)
    return SALTS_EINVAL;
  slot = view->handle.slot - 1u;
  if ((size_t)slot >= impl->capacity) return SALTS_ENOENT;
  entry = &impl->entries[slot];
  connection_index = cnet_write_connection_index(impl, view->connection);
  if (connection_index == SIZE_MAX || impl->heads[connection_index] != slot ||
      entry->generation != view->handle.generation ||
      view->_token != cnet_write_token(slot, entry->generation) ||
      !cnet_write_entry_matches(entry, view->connection))
    return SALTS_ENOENT;
  if (entry->offset > entry->size || bytes > entry->size - entry->offset) return SALTS_ERANGE;

  entry->offset += bytes;
  return cnet_write_queue_peek(queue, view->connection, view);
}

int cnet_write_queue_settle(cnet_write_queue *queue, cnet_write_view *view) {
  cnet_write_queue_impl *impl = cnet_write_impl(queue);
  cnet_write_entry *entry;
  size_t connection_index;
  uint32_t slot;
  uint32_t next;

  if (impl == NULL || view == NULL || !cnet_write_handle_valid(view->handle)) return SALTS_EINVAL;
  slot = view->handle.slot - 1u;
  if ((size_t)slot >= impl->capacity) return SALTS_ENOENT;
  entry = &impl->entries[slot];
  connection_index = cnet_write_connection_index(impl, view->connection);
  if (connection_index == SIZE_MAX || impl->heads[connection_index] != slot ||
      entry->generation != view->handle.generation ||
      view->_token != cnet_write_token(slot, entry->generation) ||
      !cnet_write_entry_matches(entry, view->connection))
    return SALTS_ENOENT;

  next = entry->next;
  if (entry->copied_bytes > impl->copied_bytes || impl->counts[connection_index] == 0u ||
      impl->live_writes == 0u)
    return SALTS_EPROTO;

  impl->heads[connection_index] = next;
  --impl->counts[connection_index];
  if (next == CNET_WRITE_SLOT_NONE) impl->tails[connection_index] = CNET_WRITE_SLOT_NONE;
  impl->copied_bytes -= entry->copied_bytes;
  --impl->live_writes;

  mem_buffer_release(entry->payload);
  entry->connection = (cnet_session_handle){0};
  entry->payload = NULL;
  entry->size = 0u;
  entry->offset = 0u;
  entry->copied_bytes = 0u;
  entry->next = CNET_WRITE_SLOT_NONE;
  entry->close_after_send = false;
  entry->state = CNET_WRITE_ENTRY_FREE;
  impl->free_slots[impl->free_count++] = slot;
  *view = (cnet_write_view){0};
  return SALTS_OK;
}

int cnet_write_queue_close(cnet_write_queue *queue) {
  cnet_write_queue_impl *impl = cnet_write_impl(queue);
  if (impl == NULL) return SALTS_EINVAL;
  if (!impl->admission_open) return SALTS_EALREADY;
  impl->admission_open = false;
  return SALTS_OK;
}

bool cnet_write_queue_get_stats(const cnet_write_queue *queue,
                                cnet_write_queue_stats *out_stats) {
  const cnet_write_queue_impl *impl = cnet_write_const_impl(queue);
  if (impl == NULL || out_stats == NULL) return false;
  *out_stats = (cnet_write_queue_stats){
      .live_writes = impl->live_writes,
      .peak_writes = impl->peak_writes,
      .copied_bytes = impl->copied_bytes,
      .peak_copied_bytes = impl->peak_copied_bytes,
      .capacity = impl->capacity,
      .connection_capacity = impl->connection_capacity,
      .admission_open = impl->admission_open};
  return true;
}

int cnet_write_queue_destroy(cnet_write_queue *queue) {
  cnet_write_queue_impl *impl = cnet_write_impl(queue);
  if (queue == NULL) return SALTS_EINVAL;
  if (impl == NULL) return SALTS_OK;
  if (impl->admission_open || impl->live_writes != 0u || impl->free_count != impl->capacity)
    return SALTS_EBUSY;

  free(impl->counts);
  free(impl->tails);
  free(impl->heads);
  free(impl->free_slots);
  free(impl->entries);
  mem_destroy(&impl->payload_pool);
  free(impl);
  queue->impl = NULL;
  return SALTS_OK;
}
