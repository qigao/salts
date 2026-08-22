#ifndef TURBO_QUEUE_H
#define TURBO_QUEUE_H

#include <turbostl/deque.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  deque_t raw;
} queue_t;

static inline turbostl_status queue_init(queue_t *queue,
                                                 const cmeta_type_desc *type,
                                                 size_t element_limit) {
  return queue == NULL ? TURBO_STL_INVALID_ARGUMENT
                       : deque_init(&queue->raw, type, element_limit);
}
static inline turbostl_status queue_init_bytes(queue_t *queue, size_t elem_size,
                                                       size_t elem_align,
                                                       size_t element_limit) {
  return queue == NULL ? TURBO_STL_INVALID_ARGUMENT
                       : deque_init_bytes(&queue->raw, elem_size, elem_align,
                                                element_limit);
}
static inline turbostl_status queue_from_array(
    queue_t *queue, const void *elements, size_t count,
    const cmeta_type_desc *type, size_t element_limit) {
  return queue == NULL ? TURBO_STL_INVALID_ARGUMENT
                       : deque_from_array(&queue->raw, elements, count,
                                                type, element_limit);
}
static inline turbostl_status queue_from_array_bytes(
    queue_t *queue, const void *elements, size_t count, size_t elem_size,
    size_t elem_align, size_t element_limit) {
  return queue == NULL ? TURBO_STL_INVALID_ARGUMENT
                       : deque_from_array_bytes(&queue->raw, elements, count, elem_size, elem_align,
                                                      element_limit);
}
static inline void queue_destroy(queue_t *queue) {
  if (queue != NULL) deque_destroy(&queue->raw);
}
static inline void queue_clear(queue_t *queue) {
  if (queue != NULL) deque_clear(&queue->raw);
}
static inline int queue_reserve(queue_t *queue, size_t capacity) {
  return queue == NULL ? TURBO_STL_INVALID_ARGUMENT : deque_reserve(&queue->raw, capacity);
}
static inline int queue_push(queue_t *queue, const void *elem) {
  return queue == NULL ? TURBO_STL_INVALID_ARGUMENT : deque_push_back(&queue->raw, elem);
}
static inline int queue_pop(queue_t *queue, void *out_elem) {
  return queue == NULL ? TURBO_STL_INVALID_ARGUMENT : deque_pop_front(&queue->raw, out_elem);
}
static inline void *queue_front(queue_t *queue) {
  return queue == NULL ? NULL : deque_front(&queue->raw);
}
static inline const void *queue_front_const(const queue_t *queue) {
  return queue == NULL ? NULL : deque_front_const(&queue->raw);
}
static inline void *queue_back(queue_t *queue) {
  return queue == NULL ? NULL : deque_back(&queue->raw);
}
static inline const void *queue_back_const(const queue_t *queue) {
  return queue == NULL ? NULL : deque_back_const(&queue->raw);
}
static inline const void *queue_at_const(const queue_t *queue, size_t index) {
  return queue == NULL ? NULL : deque_at_const(&queue->raw, index);
}
static inline size_t queue_size(const queue_t *queue) {
  return queue == NULL ? 0U : deque_size(&queue->raw);
}
static inline size_t queue_capacity(const queue_t *queue) {
  return queue == NULL ? 0U : deque_capacity(&queue->raw);
}
static inline uint64_t queue_generation(const queue_t *queue) {
  return queue == NULL ? UINT64_C(0) : deque_generation(&queue->raw);
}
static inline bool queue_empty(const queue_t *queue) {
  return queue == NULL || deque_empty(&queue->raw);
}

#ifdef __cplusplus
}
#endif
#endif /* TURBO_QUEUE_H */
