#ifndef TURBO_QUEUE_H
#define TURBO_QUEUE_H

#include <turbostl/deque.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  turbo_deque_t raw;
} turbo_queue_t;

static inline turbo_stl_status turbo_queue_init(turbo_queue_t *queue,
                                                 const cmeta_type_desc *type,
                                                 size_t element_limit) {
  return queue == NULL ? TURBO_STL_INVALID_ARGUMENT
                       : turbo_deque_init(&queue->raw, type, element_limit);
}
static inline turbo_stl_status turbo_queue_init_bytes(turbo_queue_t *queue, size_t elem_size,
                                                       size_t elem_align,
                                                       size_t element_limit) {
  return queue == NULL ? TURBO_STL_INVALID_ARGUMENT
                       : turbo_deque_init_bytes(&queue->raw, elem_size, elem_align,
                                                element_limit);
}
static inline turbo_stl_status turbo_queue_from_array(
    turbo_queue_t *queue, const void *elements, size_t count,
    const cmeta_type_desc *type, size_t element_limit) {
  return queue == NULL ? TURBO_STL_INVALID_ARGUMENT
                       : turbo_deque_from_array(&queue->raw, elements, count,
                                                type, element_limit);
}
static inline turbo_stl_status turbo_queue_from_array_bytes(
    turbo_queue_t *queue, const void *elements, size_t count, size_t elem_size,
    size_t elem_align, size_t element_limit) {
  return queue == NULL ? TURBO_STL_INVALID_ARGUMENT
                       : turbo_deque_from_array_bytes(&queue->raw, elements, count, elem_size, elem_align,
                                                      element_limit);
}
static inline void turbo_queue_destroy(turbo_queue_t *queue) {
  if (queue != NULL) turbo_deque_destroy(&queue->raw);
}
static inline void turbo_queue_clear(turbo_queue_t *queue) {
  if (queue != NULL) turbo_deque_clear(&queue->raw);
}
static inline int turbo_queue_reserve(turbo_queue_t *queue, size_t capacity) {
  return queue == NULL ? TURBO_STL_INVALID_ARGUMENT : turbo_deque_reserve(&queue->raw, capacity);
}
static inline int turbo_queue_push(turbo_queue_t *queue, const void *elem) {
  return queue == NULL ? TURBO_STL_INVALID_ARGUMENT : turbo_deque_push_back(&queue->raw, elem);
}
static inline int turbo_queue_pop(turbo_queue_t *queue, void *out_elem) {
  return queue == NULL ? TURBO_STL_INVALID_ARGUMENT : turbo_deque_pop_front(&queue->raw, out_elem);
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
static inline uint64_t turbo_queue_generation(const turbo_queue_t *queue) {
  return queue == NULL ? UINT64_C(0) : turbo_deque_generation(&queue->raw);
}
static inline bool turbo_queue_empty(const turbo_queue_t *queue) {
  return queue == NULL || turbo_deque_empty(&queue->raw);
}

#ifdef __cplusplus
}
#endif
#endif /* TURBO_QUEUE_H */
