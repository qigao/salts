/**
 * @file vec.h
 * @brief Dynamic array implementation.
 *
 * THREAD SAFETY: NOT thread-safe. Each vec_t instance must be accessed by only
 * one thread at a time. Use external synchronization for shared vectors.
 */

#ifndef TURBOSTL_VEC_H
#define TURBOSTL_VEC_H

#include <turbostl/status.h>

#include <cmeta/cmeta.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vec {
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
} vec_t;

/* Handles must be first initialized with `{0}`. A destroyed handle may be
 * reused. init/from_array on a live handle return STL_INVALID_ARGUMENT without
 * mutation. Borrowed pointers from at/data become invalid after any successful
 * mutation, storage-changing reserve, clear, or destroy. */
stl_status vec_init(vec_t *vec, const cmeta_type_desc *element_type,
                    size_t element_limit);
stl_status vec_init_bytes(vec_t *vec, size_t elem_size, size_t elem_align,
                          size_t element_limit);
stl_status vec_from_array(vec_t *vec, const void *elements, size_t count,
                          const cmeta_type_desc *element_type,
                          size_t element_limit);
stl_status vec_from_array_bytes(vec_t *vec, const void *elements, size_t count,
                                size_t elem_size, size_t elem_align,
                                size_t element_limit);
void vec_destroy(vec_t *vec);
stl_status vec_clear(vec_t *vec);
stl_status vec_reserve(vec_t *vec, size_t min_capacity);
stl_status vec_resize(vec_t *vec, size_t new_size);
stl_status vec_push(vec_t *vec, const void *elem);
stl_status vec_pop(vec_t *vec, void *out_elem);
stl_status vec_insert(vec_t *vec, size_t index, const void *elem);
stl_status vec_set(vec_t *vec, size_t index, const void *elem);
stl_status vec_erase(vec_t *vec, size_t index, void *out_elem);
stl_status vec_swap_remove(vec_t *vec, size_t index, void *out_elem);
void *vec_at(vec_t *vec, size_t index);
const void *vec_at_const(const vec_t *vec, size_t index);
void *vec_data(vec_t *vec);
const void *vec_data_const(const vec_t *vec);
size_t vec_size(const vec_t *vec);
size_t vec_capacity(const vec_t *vec);
uint64_t vec_generation(const vec_t *vec);
bool vec_empty(const vec_t *vec);

/* Temporary repository-migration aliases. Remove after all callers migrate. */
typedef vec_t turbo_vec_t;
#define turbo_vec_init vec_init
#define turbo_vec_init_bytes vec_init_bytes
#define turbo_vec_from_array vec_from_array
#define turbo_vec_from_array_bytes vec_from_array_bytes
#define turbo_vec_destroy vec_destroy
#define turbo_vec_clear vec_clear
#define turbo_vec_reserve vec_reserve
#define turbo_vec_resize vec_resize
#define turbo_vec_push vec_push
#define turbo_vec_pop vec_pop
#define turbo_vec_insert vec_insert
#define turbo_vec_set vec_set
#define turbo_vec_erase vec_erase
#define turbo_vec_swap_remove vec_swap_remove
#define turbo_vec_at vec_at
#define turbo_vec_at_const vec_at_const
#define turbo_vec_data vec_data
#define turbo_vec_data_const vec_data_const
#define turbo_vec_size vec_size
#define turbo_vec_capacity vec_capacity
#define turbo_vec_generation vec_generation
#define turbo_vec_empty vec_empty

#ifdef __cplusplus
}
#endif

#endif /* TURBOSTL_VEC_H */
