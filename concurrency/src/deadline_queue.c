#include <turbo/deadline_queue.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define TURBO_DEADLINE_INDEX_NONE UINT32_MAX

typedef struct turbo_deadline_entry {
  turbo_deadline_event event;
  uint64_t order;
  uint32_t heap_index;
  uint32_t generation;
  bool active;
} turbo_deadline_entry;

typedef struct turbo_deadline_queue_impl {
  turbo_deadline_entry *entries;
  uint32_t *heap;
  uint32_t *free_slots;
  size_t size;
  size_t free_count;
  size_t capacity;
  uint64_t next_order;
} turbo_deadline_queue_impl;

static turbo_deadline_queue_impl *turbo_deadline_get(turbo_deadline_queue *queue) {
  return queue != NULL ? (turbo_deadline_queue_impl *)queue->impl : NULL;
}

static const turbo_deadline_queue_impl *
turbo_deadline_const_get(const turbo_deadline_queue *queue) {
  return queue != NULL ? (const turbo_deadline_queue_impl *)queue->impl : NULL;
}

static turbo_deadline_id turbo_deadline_make_id(uint32_t slot, uint32_t generation) {
  return ((uint64_t)generation << 32u) | ((uint64_t)slot + 1u);
}

static bool turbo_deadline_decode_id(turbo_deadline_id id, uint32_t *out_slot,
                                     uint32_t *out_generation) {
  const uint32_t encoded_slot = (uint32_t)id;
  const uint32_t generation = (uint32_t)(id >> 32u);
  if (encoded_slot == 0u || generation == 0u) return false;
  *out_slot = encoded_slot - 1u;
  *out_generation = generation;
  return true;
}

static bool turbo_deadline_before(const turbo_deadline_queue_impl *impl, uint32_t left_slot,
                                  uint32_t right_slot) {
  const turbo_deadline_entry *left = &impl->entries[left_slot];
  const turbo_deadline_entry *right = &impl->entries[right_slot];
  return left->event.deadline_ms < right->event.deadline_ms ||
         (left->event.deadline_ms == right->event.deadline_ms && left->order < right->order);
}

static void turbo_deadline_swap(turbo_deadline_queue_impl *impl, size_t left, size_t right) {
  const uint32_t temporary = impl->heap[left];
  impl->heap[left] = impl->heap[right];
  impl->heap[right] = temporary;
  impl->entries[impl->heap[left]].heap_index = (uint32_t)left;
  impl->entries[impl->heap[right]].heap_index = (uint32_t)right;
}

static void turbo_deadline_sift_up(turbo_deadline_queue_impl *impl, size_t index) {
  while (index != 0u) {
    const size_t parent = (index - 1u) / 2u;
    if (!turbo_deadline_before(impl, impl->heap[index], impl->heap[parent])) break;
    turbo_deadline_swap(impl, index, parent);
    index = parent;
  }
}

static void turbo_deadline_sift_down(turbo_deadline_queue_impl *impl, size_t index) {
  for (;;) {
    const size_t left = index * 2u + 1u;
    const size_t right = left + 1u;
    size_t earliest = index;
    if (left < impl->size && turbo_deadline_before(impl, impl->heap[left], impl->heap[earliest]))
      earliest = left;
    if (right < impl->size && turbo_deadline_before(impl, impl->heap[right], impl->heap[earliest]))
      earliest = right;
    if (earliest == index) break;
    turbo_deadline_swap(impl, index, earliest);
    index = earliest;
  }
}

static void turbo_deadline_release_slot(turbo_deadline_queue_impl *impl, uint32_t slot) {
  turbo_deadline_entry *entry = &impl->entries[slot];
  memset(&entry->event, 0, sizeof(entry->event));
  entry->order = 0u;
  entry->heap_index = TURBO_DEADLINE_INDEX_NONE;
  entry->active = false;
  if (entry->generation == UINT32_MAX) {
    entry->generation = 0u;
    return;
  }
  ++entry->generation;
  impl->free_slots[impl->free_count++] = slot;
}

static void turbo_deadline_remove(turbo_deadline_queue_impl *impl, size_t heap_index,
                                  turbo_deadline_event *out_event) {
  const uint32_t removed_slot = impl->heap[heap_index];
  *out_event = impl->entries[removed_slot].event;
  --impl->size;
  if (heap_index != impl->size) {
    impl->heap[heap_index] = impl->heap[impl->size];
    impl->entries[impl->heap[heap_index]].heap_index = (uint32_t)heap_index;
    if (heap_index != 0u &&
        turbo_deadline_before(impl, impl->heap[heap_index], impl->heap[(heap_index - 1u) / 2u]))
      turbo_deadline_sift_up(impl, heap_index);
    else turbo_deadline_sift_down(impl, heap_index);
  }
  impl->heap[impl->size] = TURBO_DEADLINE_INDEX_NONE;
  turbo_deadline_release_slot(impl, removed_slot);
}

int turbo_deadline_queue_init(turbo_deadline_queue *queue, size_t capacity) {
  turbo_deadline_queue_impl *impl;
  size_t index;
  if (queue == NULL || capacity == 0u) return TURBO_EINVAL;
  if (queue->impl != NULL) return TURBO_EALREADY;
  if (capacity > UINT32_MAX || capacity > SIZE_MAX / sizeof(turbo_deadline_entry) ||
      capacity > SIZE_MAX / sizeof(uint32_t))
    return TURBO_ERANGE;
  impl = (turbo_deadline_queue_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return TURBO_ENOMEM;
  impl->entries = (turbo_deadline_entry *)calloc(capacity, sizeof(*impl->entries));
  impl->heap = (uint32_t *)malloc(capacity * sizeof(*impl->heap));
  impl->free_slots = (uint32_t *)malloc(capacity * sizeof(*impl->free_slots));
  if (impl->entries == NULL || impl->heap == NULL || impl->free_slots == NULL) {
    free(impl->free_slots);
    free(impl->heap);
    free(impl->entries);
    free(impl);
    return TURBO_ENOMEM;
  }
  for (index = 0u; index < capacity; ++index) {
    impl->entries[index].generation = 1u;
    impl->entries[index].heap_index = TURBO_DEADLINE_INDEX_NONE;
    impl->heap[index] = TURBO_DEADLINE_INDEX_NONE;
    impl->free_slots[index] = (uint32_t)(capacity - index - 1u);
  }
  impl->capacity = capacity;
  impl->free_count = capacity;
  queue->impl = impl;
  return TURBO_OK;
}

int turbo_deadline_queue_schedule(turbo_deadline_queue *queue, uint64_t deadline_ms, uint64_t token,
                                  turbo_deadline_id *out_id) {
  turbo_deadline_queue_impl *impl = turbo_deadline_get(queue);
  turbo_deadline_entry *entry;
  uint32_t slot;
  size_t heap_index;
  if (out_id == NULL) return TURBO_EINVAL;
  *out_id = 0u;
  if (impl == NULL) return TURBO_EINVAL;
  if (impl->free_count == 0u) return TURBO_ENOBUFS;
  if (impl->next_order == UINT64_MAX) return TURBO_ERANGE;
  slot = impl->free_slots[--impl->free_count];
  entry = &impl->entries[slot];
  if (entry->active || entry->generation == 0u) return TURBO_EPROTO;
  heap_index = impl->size++;
  entry->event =
      (turbo_deadline_event){turbo_deadline_make_id(slot, entry->generation), deadline_ms, token};
  entry->order = impl->next_order++;
  entry->heap_index = (uint32_t)heap_index;
  entry->active = true;
  impl->heap[heap_index] = slot;
  *out_id = entry->event.id;
  turbo_deadline_sift_up(impl, heap_index);
  return TURBO_OK;
}

int turbo_deadline_queue_cancel(turbo_deadline_queue *queue, turbo_deadline_id id,
                                turbo_deadline_event *out_event) {
  turbo_deadline_queue_impl *impl = turbo_deadline_get(queue);
  turbo_deadline_entry *entry;
  uint32_t slot;
  uint32_t generation;
  if (out_event == NULL) return TURBO_EINVAL;
  memset(out_event, 0, sizeof(*out_event));
  if (impl == NULL || !turbo_deadline_decode_id(id, &slot, &generation)) return TURBO_EINVAL;
  if ((size_t)slot >= impl->capacity) return TURBO_ENOENT;
  entry = &impl->entries[slot];
  if (!entry->active || entry->generation != generation) return TURBO_ENOENT;
  turbo_deadline_remove(impl, entry->heap_index, out_event);
  return TURBO_OK;
}

int turbo_deadline_queue_peek(const turbo_deadline_queue *queue, turbo_deadline_event *out_event) {
  const turbo_deadline_queue_impl *impl = turbo_deadline_const_get(queue);
  if (out_event == NULL) return TURBO_EINVAL;
  memset(out_event, 0, sizeof(*out_event));
  if (impl == NULL) return TURBO_EINVAL;
  if (impl->size == 0u) return TURBO_ETIMEDOUT;
  *out_event = impl->entries[impl->heap[0]].event;
  return TURBO_OK;
}

int turbo_deadline_queue_take_ready(turbo_deadline_queue *queue, uint64_t now_ms,
                                    turbo_deadline_event *out_event) {
  turbo_deadline_queue_impl *impl = turbo_deadline_get(queue);
  if (out_event == NULL) return TURBO_EINVAL;
  memset(out_event, 0, sizeof(*out_event));
  if (impl == NULL) return TURBO_EINVAL;
  if (impl->size == 0u || impl->entries[impl->heap[0]].event.deadline_ms > now_ms)
    return TURBO_ETIMEDOUT;
  turbo_deadline_remove(impl, 0u, out_event);
  return TURBO_OK;
}

size_t turbo_deadline_queue_size(const turbo_deadline_queue *queue) {
  const turbo_deadline_queue_impl *impl = turbo_deadline_const_get(queue);
  return impl != NULL ? impl->size : 0u;
}

int turbo_deadline_queue_destroy(turbo_deadline_queue *queue) {
  turbo_deadline_queue_impl *impl = turbo_deadline_get(queue);
  if (queue == NULL) return TURBO_EINVAL;
  if (impl == NULL) return TURBO_OK;
  free(impl->free_slots);
  free(impl->heap);
  free(impl->entries);
  free(impl);
  queue->impl = NULL;
  return TURBO_OK;
}
