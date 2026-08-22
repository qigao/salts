#ifndef TURBO_DEQUE_H
#define TURBO_DEQUE_H

#include <turbostl/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <cmeta/cmeta.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void *data;
  size_t size;
  size_t capacity;
  size_t elem_size;
  size_t elem_stride;
  size_t elem_align;
  size_t element_limit;
  const cmeta_type_desc *element_type;
  uint64_t generation;
  bool initialized;
  size_t head;
} deque_t;

/* Handles must be first initialized with `{0}`. A destroyed handle may be
 * reused. init/from_array on a live handle return TURBO_STL_INVALID_ARGUMENT
 * without mutation. Borrowed pointers from front/back/at become invalid after
 * any successful mutation, storage-changing reserve, clear, or destroy. */
turbostl_status deque_init(deque_t *deque,
                                                const cmeta_type_desc *element_type,
                                                size_t element_limit);
turbostl_status deque_init_bytes(deque_t *deque, size_t elem_size,
                                                      size_t elem_align, size_t element_limit);
turbostl_status deque_from_array(deque_t *deque,
                                                      const void *elements, size_t count,
                                                      const cmeta_type_desc *element_type,
                                                      size_t element_limit);
turbostl_status deque_from_array_bytes(deque_t *deque,
                                                            const void *elements, size_t count,
                                                            size_t elem_size, size_t elem_align,
                                                            size_t element_limit);
void deque_destroy(deque_t *deque);
turbostl_status deque_clear(deque_t *deque);
turbostl_status deque_reserve(deque_t *deque, size_t min_capacity);
turbostl_status deque_push_back(deque_t *deque, const void *elem);
turbostl_status deque_push_front(deque_t *deque, const void *elem);
/* A non-NULL out_elem must be sufficiently aligned, uninitialized element
 * storage; success transfers ownership there. NULL destroys the value. On
 * failure out_elem is not written. */
turbostl_status deque_pop_back(deque_t *deque, void *out_elem);
/* A non-NULL out_elem must be sufficiently aligned, uninitialized element
 * storage; success transfers ownership there. NULL destroys the value. On
 * failure out_elem is not written. */
turbostl_status deque_pop_front(deque_t *deque, void *out_elem);
turbostl_status deque_set(deque_t *deque, size_t index,
                                               const void *elem);
void *deque_front(deque_t *deque);
const void *deque_front_const(const deque_t *deque);
void *deque_back(deque_t *deque);
const void *deque_back_const(const deque_t *deque);
void *deque_at(deque_t *deque, size_t index);
const void *deque_at_const(const deque_t *deque, size_t index);
size_t deque_size(const deque_t *deque);
size_t deque_capacity(const deque_t *deque);
uint64_t deque_generation(const deque_t *deque);
bool deque_empty(const deque_t *deque);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_DEQUE_H */
