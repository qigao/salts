#ifndef CONTAINER_LIST_H
#define CONTAINER_LIST_H

#include "platform.h"
#include <container/deque.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  container_deque_t raw;
} container_list_t;

static inline int container_list_init(container_list_t *list, size_t elem_size) {
  return container_deque_init(&list->raw, elem_size);
}

static inline void container_list_destroy(container_list_t *list) {
  container_deque_destroy(&list->raw);
}

static inline void container_list_clear(container_list_t *list) {
  container_deque_clear(&list->raw);
}

static inline int container_list_reserve(container_list_t *list, size_t min_capacity) {
  return container_deque_reserve(&list->raw, min_capacity);
}

static inline int container_list_push_front(container_list_t *list, const void *elem) {
  return container_deque_push_front(&list->raw, elem);
}

static inline int container_list_push_back(container_list_t *list, const void *elem) {
  return container_deque_push_back(&list->raw, elem);
}

static inline int container_list_pop_front(container_list_t *list, void *out_elem) {
  return container_deque_pop_front(&list->raw, out_elem);
}

static inline int container_list_pop_back(container_list_t *list, void *out_elem) {
  return container_deque_pop_back(&list->raw, out_elem);
}

static inline void *container_list_front(container_list_t *list) {
  return container_deque_front(&list->raw);
}

static inline const void *container_list_front_const(const container_list_t *list) {
  return container_deque_front_const(&list->raw);
}

static inline void *container_list_back(container_list_t *list) {
  return container_deque_back(&list->raw);
}

static inline const void *container_list_back_const(const container_list_t *list) {
  return container_deque_back_const(&list->raw);
}

static inline void *container_list_at(container_list_t *list, size_t index) {
  return container_deque_at(&list->raw, index);
}

static inline const void *container_list_at_const(const container_list_t *list, size_t index) {
  return container_deque_at_const(&list->raw, index);
}

static inline size_t container_list_size(const container_list_t *list) {
  return container_deque_size(&list->raw);
}

static inline size_t container_list_capacity(const container_list_t *list) {
  return container_deque_capacity(&list->raw);
}

static inline bool container_list_empty(const container_list_t *list) {
  return container_deque_empty(&list->raw);
}

/**
 * Initialize a list by copying count fixed-size elements from a contiguous array.
 *
 * elements may be NULL only when count is zero. On allocation failure, the
 * partially initialized list is destroyed and the error is returned.
 */
static inline int container_list_from_array(container_list_t *list, const void *elements,
                                        size_t count, size_t elem_size) {
  return list == NULL
             ? TURBO_EINVAL
             : container_deque_from_array(&list->raw, elements, count, elem_size);
}


#ifdef __cplusplus
}
#endif

#endif /* CONTAINER_LIST_H */
