#include "cnet_owner_handoff.h"

#include <salts/disruptor.h>
#include <salts/error_codes.h>
#include <salts_buffer.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct cnet_owner_handoff_entry {
  cnet_owner_handoff_kind kind;
  cnet_session_handle connection;
  mem_buffer_t *backing;
  size_t offset;
  size_t size;
  size_t demand;
  bool close_after_send;
} cnet_owner_handoff_entry;

typedef struct cnet_owner_handoff_impl {
  disruptor_t *ring;
  size_t capacity;
  size_t max_payload_bytes;
  atomic_size_t live;
  atomic_size_t peak;
  _Atomic uint64_t accepted;
  _Atomic uint64_t rejected_full;
  _Atomic uint64_t rejected_closed;
  atomic_size_t publisher_entrants;
  atomic_bool admission_open;
  atomic_bool close_complete;
  uint64_t borrowed_sequence;
} cnet_owner_handoff_impl;

static cnet_owner_handoff_impl *cnet_owner_handoff_impl_get(
    cnet_owner_handoff *handoff) {
  return handoff != NULL ? (cnet_owner_handoff_impl *)handoff->impl : NULL;
}

static const cnet_owner_handoff_impl *cnet_owner_handoff_impl_const(
    const cnet_owner_handoff *handoff) {
  return handoff != NULL
             ? (const cnet_owner_handoff_impl *)handoff->impl
             : NULL;
}

static bool cnet_owner_handoff_power_of_two(size_t value) {
  return value != 0u && (value & (value - 1u)) == 0u;
}

static void cnet_owner_handoff_peak(cnet_owner_handoff_impl *impl,
                                    size_t live) {
  size_t peak = atomic_load_explicit(&impl->peak, memory_order_relaxed);
  while (peak < live &&
         !atomic_compare_exchange_weak_explicit(
             &impl->peak, &peak, live,
             memory_order_relaxed, memory_order_relaxed)) {
  }
}

static int cnet_owner_handoff_slice_offset(const mem_slice_t *slice,
                                           size_t *out_offset) {
  const char *base;
  const char *data;
  size_t used;
  uintptr_t base_value;
  uintptr_t data_value;
  uintptr_t delta;

  if (out_offset == NULL) return SALTS_EINVAL;
  *out_offset = 0u;
  if (slice == NULL || slice->buffer == NULL ||
      slice->data == NULL || slice->length == 0u)
    return SALTS_EINVAL;

  base = mem_buffer_const_data(slice->buffer);
  used = mem_buffer_used(slice->buffer);
  if (base == NULL || used == 0u) return SALTS_EINVAL;

  data = slice->data;
  base_value = (uintptr_t)(const void *)base;
  data_value = (uintptr_t)(const void *)data;
  if (data_value < base_value) return SALTS_EINVAL;
  delta = data_value - base_value;
  if (delta > (uintptr_t)SIZE_MAX) return SALTS_EINVAL;
  *out_offset = (size_t)delta;
  if (*out_offset >= used ||
      slice->length > used - *out_offset)
    return SALTS_EINVAL;
  return SALTS_OK;
}

static int cnet_owner_handoff_publish(
    cnet_owner_handoff_impl *impl,
    cnet_owner_handoff_kind kind,
    cnet_session_handle connection,
    mem_buffer_t *backing,
    size_t offset,
    size_t size,
    size_t demand,
    bool close_after_send) {
  disruptor_cursor_t cursor = {0};
  cnet_owner_handoff_entry *entry;
  mem_buffer_t *retained = NULL;
  size_t live;

  if (impl == NULL || !cnet_session_handle_valid(connection))
    return SALTS_EINVAL;
  if (kind == CNET_OWNER_HANDOFF_SEND_BUFFER) {
    if (backing == NULL || offset != 0u || size == 0u ||
        size != mem_buffer_used(backing) ||
        mem_buffer_const_data(backing) == NULL ||
        demand != 0u || size > impl->max_payload_bytes)
      return size > impl->max_payload_bytes ? SALTS_EMSGSIZE : SALTS_EINVAL;
  } else if (kind == CNET_OWNER_HANDOFF_SEND_SLICE) {
    if (backing == NULL || size == 0u || demand != 0u ||
        size > impl->max_payload_bytes)
      return size > impl->max_payload_bytes ? SALTS_EMSGSIZE : SALTS_EINVAL;
  } else if (kind == CNET_OWNER_HANDOFF_RECEIVE) {
    if (backing != NULL || offset != 0u || size != 0u ||
        demand == 0u || close_after_send)
      return SALTS_EINVAL;
  } else {
    return SALTS_EINVAL;
  }

  if (!atomic_load_explicit(&impl->admission_open, memory_order_acquire)) {
    atomic_fetch_add_explicit(&impl->rejected_closed, 1u, memory_order_relaxed);
    return SALTS_ESHUTDOWN;
  }

  atomic_fetch_add_explicit(&impl->publisher_entrants, 1u, memory_order_acq_rel);
  if (!atomic_load_explicit(&impl->admission_open, memory_order_acquire)) {
    atomic_fetch_sub_explicit(&impl->publisher_entrants, 1u, memory_order_release);
    atomic_fetch_add_explicit(&impl->rejected_closed, 1u, memory_order_relaxed);
    return SALTS_ESHUTDOWN;
  }

  if (backing != NULL) {
    retained = mem_buffer_retain(backing);
    if (retained == NULL) {
      atomic_fetch_sub_explicit(&impl->publisher_entrants, 1u, memory_order_release);
      return SALTS_EINVAL;
    }
  }

  if (!disruptor_publisher_try_claim(impl->ring, &cursor)) {
    mem_buffer_release(retained);
    atomic_fetch_sub_explicit(&impl->publisher_entrants, 1u, memory_order_release);
    atomic_fetch_add_explicit(&impl->rejected_full, 1u, memory_order_relaxed);
    return SALTS_ENOBUFS;
  }

  entry = (cnet_owner_handoff_entry *)disruptor_acquire_entry(
      impl->ring, &cursor);
  if (entry == NULL) {
    mem_buffer_release(retained);
    atomic_fetch_sub_explicit(&impl->publisher_entrants, 1u, memory_order_release);
    return SALTS_EPROTO;
  }

  *entry = (cnet_owner_handoff_entry){
      kind, connection, retained, offset, size, demand, close_after_send};

  live = atomic_fetch_add_explicit(
             &impl->live, 1u, memory_order_acq_rel) +
         1u;
  cnet_owner_handoff_peak(impl, live);
  atomic_fetch_add_explicit(&impl->accepted, 1u, memory_order_relaxed);
  (void)disruptor_publisher_publish(impl->ring, &cursor);
  atomic_fetch_sub_explicit(&impl->publisher_entrants, 1u, memory_order_release);
  return SALTS_OK;
}

int cnet_owner_handoff_init(cnet_owner_handoff *handoff,
                            const cnet_owner_handoff_config *config) {
  cnet_owner_handoff_impl *impl;
  disruptor_config_t ring_config;

  if (handoff == NULL || config == NULL)
    return SALTS_EINVAL;
  if (handoff->impl != NULL) return SALTS_EALREADY;
  if (!cnet_owner_handoff_power_of_two(config->capacity) ||
      config->max_payload_bytes == 0u)
    return SALTS_EINVAL;

  impl = (cnet_owner_handoff_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return SALTS_ENOMEM;

  ring_config = (disruptor_config_t){
      sizeof(cnet_owner_handoff_entry),
      config->capacity,
      1u,
      DISRUPTOR_MODE_WORKER_POOL};
  impl->ring = disruptor_create(&ring_config);
  if (impl->ring == NULL) {
    free(impl);
    return SALTS_ENOMEM;
  }

  impl->capacity = config->capacity;
  impl->max_payload_bytes = config->max_payload_bytes;
  atomic_init(&impl->live, 0u);
  atomic_init(&impl->peak, 0u);
  atomic_init(&impl->accepted, 0u);
  atomic_init(&impl->rejected_full, 0u);
  atomic_init(&impl->rejected_closed, 0u);
  atomic_init(&impl->publisher_entrants, 0u);
  atomic_init(&impl->admission_open, true);
  atomic_init(&impl->close_complete, false);
  impl->borrowed_sequence = 0u;
  handoff->impl = impl;
  return SALTS_OK;
}

int cnet_owner_handoff_publish_buffer(cnet_owner_handoff *handoff,
                                      cnet_session_handle connection,
                                      mem_buffer_t *buffer,
                                      bool close_after_send) {
  cnet_owner_handoff_impl *impl = cnet_owner_handoff_impl_get(handoff);
  const size_t size = buffer != NULL ? mem_buffer_used(buffer) : 0u;
  return cnet_owner_handoff_publish(
      impl, CNET_OWNER_HANDOFF_SEND_BUFFER, connection,
      buffer, 0u, size, 0u, close_after_send);
}

int cnet_owner_handoff_publish_slice(cnet_owner_handoff *handoff,
                                     cnet_session_handle connection,
                                     const mem_slice_t *slice,
                                     bool close_after_send) {
  cnet_owner_handoff_impl *impl = cnet_owner_handoff_impl_get(handoff);
  size_t offset = 0u;
  int status = cnet_owner_handoff_slice_offset(slice, &offset);
  if (status != SALTS_OK) return status;
  return cnet_owner_handoff_publish(
      impl, CNET_OWNER_HANDOFF_SEND_SLICE, connection,
      slice->buffer, offset, slice->length, 0u, close_after_send);
}

int cnet_owner_handoff_publish_receive(cnet_owner_handoff *handoff,
                                       cnet_session_handle connection,
                                       size_t demand) {
  return cnet_owner_handoff_publish(
      cnet_owner_handoff_impl_get(handoff),
      CNET_OWNER_HANDOFF_RECEIVE, connection,
      NULL, 0u, 0u, demand, false);
}

int cnet_owner_handoff_take(cnet_owner_handoff *handoff,
                            cnet_owner_handoff_view *out_view) {
  cnet_owner_handoff_impl *impl = cnet_owner_handoff_impl_get(handoff);
  disruptor_cursor_t cursor = {0};
  const cnet_owner_handoff_entry *entry;

  if (out_view == NULL) return SALTS_EINVAL;
  memset(out_view, 0, sizeof(*out_view));
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->borrowed_sequence != 0u) return SALTS_EBUSY;

  if (!disruptor_worker_try_claim(impl->ring, &cursor)) {
    if (atomic_load_explicit(&impl->close_complete, memory_order_acquire) &&
        atomic_load_explicit(&impl->live, memory_order_acquire) == 0u)
      return SALTS_EOF;
    return SALTS_ETIMEDOUT;
  }

  entry = (const cnet_owner_handoff_entry *)disruptor_show_entry(
      impl->ring, &cursor);
  if (entry == NULL || entry->kind == CNET_OWNER_HANDOFF_NONE) {
    disruptor_worker_release_entry(impl->ring, &cursor);
    return SALTS_EPROTO;
  }

  *out_view = (cnet_owner_handoff_view){
      entry->kind,
      entry->connection,
      entry->backing,
      entry->offset,
      entry->size,
      entry->demand,
      entry->close_after_send,
      cursor.sequence};
  impl->borrowed_sequence = cursor.sequence;
  return SALTS_OK;
}

int cnet_owner_handoff_release(cnet_owner_handoff *handoff,
                               cnet_owner_handoff_view *view) {
  cnet_owner_handoff_impl *impl = cnet_owner_handoff_impl_get(handoff);
  disruptor_cursor_t cursor;
  cnet_owner_handoff_entry *entry;

  if (impl == NULL || view == NULL || view->_sequence == 0u)
    return SALTS_EINVAL;
  if (impl->borrowed_sequence != view->_sequence)
    return SALTS_EINVAL;

  cursor.sequence = view->_sequence;
  entry = (cnet_owner_handoff_entry *)disruptor_show_entry(
      impl->ring, &cursor);
  if (entry == NULL || entry->kind != view->kind ||
      entry->connection.slot != view->connection.slot ||
      entry->connection.generation != view->connection.generation)
    return SALTS_EPROTO;

  mem_buffer_release(entry->backing);
  memset(entry, 0, sizeof(*entry));
  disruptor_worker_release_entry(impl->ring, &cursor);
  impl->borrowed_sequence = 0u;
  atomic_fetch_sub_explicit(&impl->live, 1u, memory_order_release);
  memset(view, 0, sizeof(*view));
  return SALTS_OK;
}

int cnet_owner_handoff_transfer_write(cnet_owner_handoff *handoff,
                                      cnet_owner_handoff_view *view,
                                      cnet_write_queue *writes,
                                      cnet_write_handle *out_handle) {
  mem_slice_t slice;
  int status;

  if (handoff == NULL || view == NULL || writes == NULL ||
      out_handle == NULL)
    return SALTS_EINVAL;
  *out_handle = (cnet_write_handle){0};

  if (view->kind == CNET_OWNER_HANDOFF_SEND_BUFFER) {
    status = cnet_write_queue_enqueue_buffer(
        writes, view->connection, view->backing,
        view->close_after_send, out_handle);
  } else if (view->kind == CNET_OWNER_HANDOFF_SEND_SLICE) {
    if (view->backing == NULL ||
        view->offset > mem_buffer_used(view->backing) ||
        view->size > mem_buffer_used(view->backing) - view->offset)
      return SALTS_EPROTO;
    slice = (mem_slice_t){
        mem_buffer_data(view->backing) + view->offset,
        view->size,
        view->backing};
    status = cnet_write_queue_enqueue_slice(
        writes, view->connection, &slice,
        view->close_after_send, out_handle);
  } else {
    return SALTS_EINVAL;
  }

  if (status != SALTS_OK) return status;
  return cnet_owner_handoff_release(handoff, view);
}

bool cnet_owner_handoff_get_stats(const cnet_owner_handoff *handoff,
                                  cnet_owner_handoff_stats *out_stats) {
  const cnet_owner_handoff_impl *impl =
      cnet_owner_handoff_impl_const(handoff);
  if (impl == NULL || out_stats == NULL) return false;
  *out_stats = (cnet_owner_handoff_stats){
      atomic_load_explicit(&impl->live, memory_order_acquire),
      atomic_load_explicit(&impl->peak, memory_order_acquire),
      atomic_load_explicit(&impl->accepted, memory_order_acquire),
      atomic_load_explicit(&impl->rejected_full, memory_order_acquire),
      atomic_load_explicit(&impl->rejected_closed, memory_order_acquire),
      atomic_load_explicit(&impl->admission_open, memory_order_acquire)};
  return true;
}

int cnet_owner_handoff_close(cnet_owner_handoff *handoff) {
  cnet_owner_handoff_impl *impl = cnet_owner_handoff_impl_get(handoff);

  if (impl == NULL) return SALTS_EINVAL;
  (void)atomic_exchange_explicit(
      &impl->admission_open, false, memory_order_acq_rel);
  if (atomic_load_explicit(
          &impl->publisher_entrants, memory_order_acquire) != 0u)
    return SALTS_EBUSY;
  if (atomic_exchange_explicit(
          &impl->close_complete, true, memory_order_acq_rel))
    return SALTS_EALREADY;
  disruptor_worker_wake_all(impl->ring);
  return SALTS_OK;
}

int cnet_owner_handoff_destroy(cnet_owner_handoff *handoff) {
  cnet_owner_handoff_impl *impl =
      cnet_owner_handoff_impl_get(handoff);

  if (handoff == NULL) return SALTS_EINVAL;
  if (impl == NULL) return SALTS_OK;
  if (!atomic_load_explicit(&impl->close_complete, memory_order_acquire) ||
      atomic_load_explicit(&impl->publisher_entrants, memory_order_acquire) != 0u ||
      atomic_load_explicit(&impl->live, memory_order_acquire) != 0u ||
      impl->borrowed_sequence != 0u)
    return SALTS_EBUSY;

  disruptor_destroy(impl->ring);
  free(impl);
  handoff->impl = NULL;
  return SALTS_OK;
}
