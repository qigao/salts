/**
 * @file vec.h
 * @brief Dynamic array implementation
 *
 * THREAD SAFETY: NOT thread-safe. Each vec_t instance must be accessed
 *                by only one thread at a time. Use external synchronization
 *                (e.g., mutex) for shared vectors.
 *
 * CONCURRENCY MODEL: Single-owner model. For multi-threaded scenarios:
 *                    - Use one vec per thread (thread-local), or
 *                    - Protect shared vec with external mutex, or
 *                    - Use read-write lock for concurrent reads + exclusive writes
 *
 * CONCURRENT READS: Multiple threads may safely read a const vec_t
 *                   (using vec_at_const, vec_size, etc.) if no
 *                   thread is modifying it. Reallocation invalidates pointers.
 */

#ifndef TURBO_VEC_H
#define TURBO_VEC_H

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
} vec_t;

/* Handles must be first initialized with `{0}`. A destroyed handle may be
 * reused. init/from_array on a live handle return TURBO_STL_INVALID_ARGUMENT
 * without mutation. Borrowed pointers from at/data become invalid after any
 * successful mutation, storage-changing reserve, clear, or destroy. */
turbostl_status vec_init(vec_t *vec,
                                              const cmeta_type_desc *element_type,
                                              size_t element_limit);
turbostl_status vec_init_bytes(vec_t *vec, size_t elem_size,
                                                    size_t elem_align, size_t element_limit);
turbostl_status vec_from_array(vec_t *vec, const void *elements,
                                                    size_t count,
                                                    const cmeta_type_desc *element_type,
                                                    size_t element_limit);
turbostl_status vec_from_array_bytes(vec_t *vec,
                                                          const void *elements, size_t count,
                                                          size_t elem_size, size_t elem_align,
                                                          size_t element_limit);
void vec_destroy(vec_t *vec);
turbostl_status vec_clear(vec_t *vec);
turbostl_status vec_reserve(vec_t *vec, size_t min_capacity);
turbostl_status vec_resize(vec_t *vec, size_t new_size);
turbostl_status vec_push(vec_t *vec, const void *elem);
/* A non-NULL out_elem must be sufficiently aligned, uninitialized element
 * storage; success transfers ownership there. NULL destroys the value. On
 * failure out_elem is not written. */
turbostl_status vec_pop(vec_t *vec, void *out_elem);
turbostl_status vec_insert(vec_t *vec, size_t index, const void *elem);
turbostl_status vec_set(vec_t *vec, size_t index, const void *elem);
/* A non-NULL out_elem must be sufficiently aligned, uninitialized element
 * storage; success transfers ownership there. NULL destroys the value. On
 * failure out_elem is not written. */
turbostl_status vec_erase(vec_t *vec, size_t index, void *out_elem);
/* A non-NULL out_elem must be sufficiently aligned, uninitialized element
 * storage; success transfers ownership there. NULL destroys the value. On
 * failure out_elem is not written. */
turbostl_status vec_swap_remove(vec_t *vec, size_t index, void *out_elem);
void *vec_at(vec_t *vec, size_t index);
const void *vec_at_const(const vec_t *vec, size_t index);
void *vec_data(vec_t *vec);
const void *vec_data_const(const vec_t *vec);
size_t vec_size(const vec_t *vec);
size_t vec_capacity(const vec_t *vec);
uint64_t vec_generation(const vec_t *vec);
bool vec_empty(const vec_t *vec);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_VEC_H */
