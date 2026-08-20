#ifndef CONTAINER_QUEUE_H
#define CONTAINER_QUEUE_H

#include <container/deque.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  container_deque_t raw;
} container_queue_t;

static inline int container_queue_init(container_queue_t *queue, size_t elem_size) {
  return queue == NULL ? TURBO_EINVAL : container_deque_init(&queue->raw, elem_size);
}
static inline int container_queue_from_array(container_queue_t *queue, const void *elements,
                                         size_t count, size_t elem_size) {
  return queue == NULL ? TURBO_EINVAL : container_deque_from_array(&queue->raw, elements, count, elem_size);
}
static inline void container_queue_destroy(container_queue_t *queue) {
  if (queue != NULL) container_deque_destroy(&queue->raw);
}
static inline void container_queue_clear(container_queue_t *queue) {
  if (queue != NULL) container_deque_clear(&queue->raw);
}
static inline int container_queue_reserve(container_queue_t *queue, size_t capacity) {
  return queue == NULL ? TURBO_EINVAL : container_deque_reserve(&queue->raw, capacity);
}
static inline int container_queue_push(container_queue_t *queue, const void *elem) {
  return queue == NULL ? TURBO_EINVAL : container_deque_push_back(&queue->raw, elem);
}
static inline int container_queue_pop(container_queue_t *queue, void *out_elem) {
  return queue == NULL ? TURBO_EINVAL : container_deque_pop_front(&queue->raw, out_elem);
}
static inline void *container_queue_front(container_queue_t *queue) {
  return queue == NULL ? NULL : container_deque_front(&queue->raw);
}
static inline const void *container_queue_front_const(const container_queue_t *queue) {
  return queue == NULL ? NULL : container_deque_front_const(&queue->raw);
}
static inline void *container_queue_back(container_queue_t *queue) {
  return queue == NULL ? NULL : container_deque_back(&queue->raw);
}
static inline const void *container_queue_back_const(const container_queue_t *queue) {
  return queue == NULL ? NULL : container_deque_back_const(&queue->raw);
}
static inline const void *container_queue_at_const(const container_queue_t *queue, size_t index) {
  return queue == NULL ? NULL : container_deque_at_const(&queue->raw, index);
}
static inline size_t container_queue_size(const container_queue_t *queue) {
  return queue == NULL ? 0U : container_deque_size(&queue->raw);
}
static inline size_t container_queue_capacity(const container_queue_t *queue) {
  return queue == NULL ? 0U : container_deque_capacity(&queue->raw);
}
static inline bool container_queue_empty(const container_queue_t *queue) {
  return queue == NULL || container_deque_empty(&queue->raw);
}


#ifdef __cplusplus
}
#endif
#endif /* CONTAINER_QUEUE_H */
