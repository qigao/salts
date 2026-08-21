#ifndef TURBO_QUEUE_H
#define TURBO_QUEUE_H

#include <turbo/container/deque.h>
#include <turbo/container/meta.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  turbo_deque_t raw;
} turbo_queue_t;

static inline int turbo_queue_init(turbo_queue_t *queue, size_t elem_size) {
  return queue == NULL ? CONTAINER_INVALID_ARGUMENT : turbo_deque_init(&queue->raw, elem_size);
}
static inline int turbo_queue_from_array(turbo_queue_t *queue, const void *elements,
                                         size_t count, size_t elem_size) {
  return queue == NULL ? CONTAINER_INVALID_ARGUMENT : turbo_deque_from_array(&queue->raw, elements, count, elem_size);
}
static inline void turbo_queue_destroy(turbo_queue_t *queue) {
  if (queue != NULL) turbo_deque_destroy(&queue->raw);
}
static inline void turbo_queue_clear(turbo_queue_t *queue) {
  if (queue != NULL) turbo_deque_clear(&queue->raw);
}
static inline int turbo_queue_reserve(turbo_queue_t *queue, size_t capacity) {
  return queue == NULL ? CONTAINER_INVALID_ARGUMENT : turbo_deque_reserve(&queue->raw, capacity);
}
static inline int turbo_queue_push(turbo_queue_t *queue, const void *elem) {
  return queue == NULL ? CONTAINER_INVALID_ARGUMENT : turbo_deque_push_back(&queue->raw, elem);
}
static inline int turbo_queue_pop(turbo_queue_t *queue, void *out_elem) {
  return queue == NULL ? CONTAINER_INVALID_ARGUMENT : turbo_deque_pop_front(&queue->raw, out_elem);
}
static inline void *turbo_queue_front(turbo_queue_t *queue) {
  return queue == NULL ? NULL : turbo_deque_front(&queue->raw);
}
static inline const void *turbo_queue_front_const(const turbo_queue_t *queue) {
  return queue == NULL ? NULL : turbo_deque_front_const(&queue->raw);
}
static inline void *turbo_queue_back(turbo_queue_t *queue) {
  return queue == NULL ? NULL : turbo_deque_back(&queue->raw);
}
static inline const void *turbo_queue_back_const(const turbo_queue_t *queue) {
  return queue == NULL ? NULL : turbo_deque_back_const(&queue->raw);
}
static inline const void *turbo_queue_at_const(const turbo_queue_t *queue, size_t index) {
  return queue == NULL ? NULL : turbo_deque_at_const(&queue->raw, index);
}
static inline size_t turbo_queue_size(const turbo_queue_t *queue) {
  return queue == NULL ? 0U : turbo_deque_size(&queue->raw);
}
static inline size_t turbo_queue_capacity(const turbo_queue_t *queue) {
  return queue == NULL ? 0U : turbo_deque_capacity(&queue->raw);
}
static inline bool turbo_queue_empty(const turbo_queue_t *queue) {
  return queue == NULL || turbo_deque_empty(&queue->raw);
}

#define TURBO_QUEUE_DEFINE(name, type) \
  CMETA_CONTAINER1_DEFINE(name, type, turbo_queue_t, turbo_queue, CONTAINER_OK, _, TURBO_META_QUEUE_METHODS) \
  CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name, type, turbo_queue, \
      CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE)

#ifdef __cplusplus
}
#endif
#endif /* TURBO_QUEUE_H */
