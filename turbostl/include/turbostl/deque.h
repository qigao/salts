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
} turbo_deque_t;

/* Handles must be first initialized with `{0}`. A destroyed handle may be
 * reused. init/from_array on a live handle return TURBO_STL_INVALID_ARGUMENT
 * without mutation. Borrowed pointers from front/back/at become invalid after
 * any successful mutation, storage-changing reserve, clear, or destroy. */
turbo_stl_status turbo_deque_init(turbo_deque_t *deque,
                                                const cmeta_type_desc *element_type,
                                                size_t element_limit);
turbo_stl_status turbo_deque_init_bytes(turbo_deque_t *deque, size_t elem_size,
                                                      size_t elem_align, size_t element_limit);
turbo_stl_status turbo_deque_from_array(turbo_deque_t *deque,
                                                      const void *elements, size_t count,
                                                      const cmeta_type_desc *element_type,
                                                      size_t element_limit);
turbo_stl_status turbo_deque_from_array_bytes(turbo_deque_t *deque,
                                                            const void *elements, size_t count,
                                                            size_t elem_size, size_t elem_align,
                                                            size_t element_limit);
void turbo_deque_destroy(turbo_deque_t *deque);
turbo_stl_status turbo_deque_clear(turbo_deque_t *deque);
turbo_stl_status turbo_deque_reserve(turbo_deque_t *deque, size_t min_capacity);
turbo_stl_status turbo_deque_push_back(turbo_deque_t *deque, const void *elem);
turbo_stl_status turbo_deque_push_front(turbo_deque_t *deque, const void *elem);
/* A non-NULL out_elem must be sufficiently aligned, uninitialized element
 * storage; success transfers ownership there. NULL destroys the value. On
 * failure out_elem is not written. */
turbo_stl_status turbo_deque_pop_back(turbo_deque_t *deque, void *out_elem);
/* A non-NULL out_elem must be sufficiently aligned, uninitialized element
 * storage; success transfers ownership there. NULL destroys the value. On
 * failure out_elem is not written. */
turbo_stl_status turbo_deque_pop_front(turbo_deque_t *deque, void *out_elem);
turbo_stl_status turbo_deque_set(turbo_deque_t *deque, size_t index,
                                               const void *elem);
void *turbo_deque_front(turbo_deque_t *deque);
const void *turbo_deque_front_const(const turbo_deque_t *deque);
void *turbo_deque_back(turbo_deque_t *deque);
const void *turbo_deque_back_const(const turbo_deque_t *deque);
void *turbo_deque_at(turbo_deque_t *deque, size_t index);
const void *turbo_deque_at_const(const turbo_deque_t *deque, size_t index);
size_t turbo_deque_size(const turbo_deque_t *deque);
size_t turbo_deque_capacity(const turbo_deque_t *deque);
uint64_t turbo_deque_generation(const turbo_deque_t *deque);
bool turbo_deque_empty(const turbo_deque_t *deque);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_DEQUE_H */
