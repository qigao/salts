#ifndef TURBO_LIST_H
#define TURBO_LIST_H

#include <turbo/container/deque.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  turbo_deque_t raw;
} turbo_list_t;

static inline container_status turbo_list_init(turbo_list_t *list,
                                                const cmeta_type_desc *type,
                                                size_t element_limit) {
  return list == NULL ? CONTAINER_INVALID_ARGUMENT
                      : turbo_deque_init(&list->raw, type, element_limit);
}

static inline container_status turbo_list_init_bytes(turbo_list_t *list, size_t elem_size,
                                                      size_t elem_align,
                                                      size_t element_limit) {
  return list == NULL ? CONTAINER_INVALID_ARGUMENT
                      : turbo_deque_init_bytes(&list->raw, elem_size, elem_align,
                                               element_limit);
}

static inline void turbo_list_destroy(turbo_list_t *list) {
  turbo_deque_destroy(&list->raw);
}

static inline void turbo_list_clear(turbo_list_t *list) {
  turbo_deque_clear(&list->raw);
}

static inline int turbo_list_reserve(turbo_list_t *list, size_t min_capacity) {
  return turbo_deque_reserve(&list->raw, min_capacity);
}

static inline int turbo_list_push_front(turbo_list_t *list, const void *elem) {
  return turbo_deque_push_front(&list->raw, elem);
}

static inline int turbo_list_push_back(turbo_list_t *list, const void *elem) {
  return turbo_deque_push_back(&list->raw, elem);
}

static inline int turbo_list_pop_front(turbo_list_t *list, void *out_elem) {
  return turbo_deque_pop_front(&list->raw, out_elem);
}

static inline int turbo_list_pop_back(turbo_list_t *list, void *out_elem) {
  return turbo_deque_pop_back(&list->raw, out_elem);
}

static inline void *turbo_list_front(turbo_list_t *list) {
  return turbo_deque_front(&list->raw);
}

static inline const void *turbo_list_front_const(const turbo_list_t *list) {
  return turbo_deque_front_const(&list->raw);
}

static inline void *turbo_list_back(turbo_list_t *list) {
  return turbo_deque_back(&list->raw);
}

static inline const void *turbo_list_back_const(const turbo_list_t *list) {
  return turbo_deque_back_const(&list->raw);
}

static inline void *turbo_list_at(turbo_list_t *list, size_t index) {
  return turbo_deque_at(&list->raw, index);
}

static inline const void *turbo_list_at_const(const turbo_list_t *list, size_t index) {
  return turbo_deque_at_const(&list->raw, index);
}

static inline size_t turbo_list_size(const turbo_list_t *list) {
  return turbo_deque_size(&list->raw);
}

static inline size_t turbo_list_capacity(const turbo_list_t *list) {
  return turbo_deque_capacity(&list->raw);
}

static inline uint64_t turbo_list_generation(const turbo_list_t *list) {
  return list == NULL ? UINT64_C(0) : turbo_deque_generation(&list->raw);
}

static inline bool turbo_list_empty(const turbo_list_t *list) {
  return turbo_deque_empty(&list->raw);
}

/**
 * Initialize a list by copying count fixed-size elements from a contiguous array.
 *
 * elements may be NULL only when count is zero. On allocation failure, the
 * partially initialized list is destroyed and the error is returned.
 */
static inline container_status turbo_list_from_array(
    turbo_list_t *list, const void *elements, size_t count,
    const cmeta_type_desc *type, size_t element_limit) {
  return list == NULL
             ? CONTAINER_INVALID_ARGUMENT
             : turbo_deque_from_array(&list->raw, elements, count, type,
                                      element_limit);
}

static inline container_status turbo_list_from_array_bytes(
    turbo_list_t *list, const void *elements, size_t count,
    size_t elem_size, size_t elem_align, size_t element_limit) {
  return list == NULL
             ? CONTAINER_INVALID_ARGUMENT
             : turbo_deque_from_array_bytes(&list->raw, elements, count, elem_size, elem_align,
                                            element_limit);
}

#ifdef __cplusplus
}
#endif

#endif /* TURBO_LIST_H */
