/**
 * @file turbo_vec.h
 * @brief Dynamic array implementation
 *
 * THREAD SAFETY: NOT thread-safe. Each turbo_vec_t instance must be accessed
 *                by only one thread at a time. Use external synchronization
 *                (e.g., mutex) for shared vectors.
 *
 * CONCURRENCY MODEL: Single-owner model. For multi-threaded scenarios:
 *                    - Use one vec per thread (thread-local), or
 *                    - Protect shared vec with external mutex, or
 *                    - Use read-write lock for concurrent reads + exclusive writes
 *
 * CONCURRENT READS: Multiple threads may safely read a const turbo_vec_t
 *                   (using turbo_vec_at_const, turbo_vec_size, etc.) if no
 *                   thread is modifying it. Reallocation invalidates pointers.
 */

#ifndef TURBO_VEC_H
#define TURBO_VEC_H

#include <turbo/stl/export.h>
#include <turbo/stl/status.h>

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
} turbo_vec_t;

/* Handles must be first initialized with `{0}`. A destroyed handle may be
 * reused. init/from_array on a live handle return TURBO_STL_INVALID_ARGUMENT
 * without mutation. Borrowed pointers from at/data become invalid after any
 * successful mutation, storage-changing reserve, clear, or destroy. */
TURBO_STL_API turbo_stl_status turbo_vec_init(turbo_vec_t *vec,
                                              const cmeta_type_desc *element_type,
                                              size_t element_limit);
TURBO_STL_API turbo_stl_status turbo_vec_init_bytes(turbo_vec_t *vec, size_t elem_size,
                                                    size_t elem_align, size_t element_limit);
TURBO_STL_API turbo_stl_status turbo_vec_from_array(turbo_vec_t *vec, const void *elements,
                                                    size_t count,
                                                    const cmeta_type_desc *element_type,
                                                    size_t element_limit);
TURBO_STL_API turbo_stl_status turbo_vec_from_array_bytes(turbo_vec_t *vec,
                                                          const void *elements, size_t count,
                                                          size_t elem_size, size_t elem_align,
                                                          size_t element_limit);
TURBO_STL_API void turbo_vec_destroy(turbo_vec_t *vec);
TURBO_STL_API turbo_stl_status turbo_vec_clear(turbo_vec_t *vec);
TURBO_STL_API turbo_stl_status turbo_vec_reserve(turbo_vec_t *vec, size_t min_capacity);
TURBO_STL_API turbo_stl_status turbo_vec_resize(turbo_vec_t *vec, size_t new_size);
TURBO_STL_API turbo_stl_status turbo_vec_push(turbo_vec_t *vec, const void *elem);
/* A non-NULL out_elem must be sufficiently aligned, uninitialized element
 * storage; success transfers ownership there. NULL destroys the value. On
 * failure out_elem is not written. */
TURBO_STL_API turbo_stl_status turbo_vec_pop(turbo_vec_t *vec, void *out_elem);
TURBO_STL_API turbo_stl_status turbo_vec_insert(turbo_vec_t *vec, size_t index, const void *elem);
TURBO_STL_API turbo_stl_status turbo_vec_set(turbo_vec_t *vec, size_t index, const void *elem);
/* A non-NULL out_elem must be sufficiently aligned, uninitialized element
 * storage; success transfers ownership there. NULL destroys the value. On
 * failure out_elem is not written. */
TURBO_STL_API turbo_stl_status turbo_vec_erase(turbo_vec_t *vec, size_t index, void *out_elem);
/* A non-NULL out_elem must be sufficiently aligned, uninitialized element
 * storage; success transfers ownership there. NULL destroys the value. On
 * failure out_elem is not written. */
TURBO_STL_API turbo_stl_status turbo_vec_swap_remove(turbo_vec_t *vec, size_t index, void *out_elem);
TURBO_STL_API void *turbo_vec_at(turbo_vec_t *vec, size_t index);
TURBO_STL_API const void *turbo_vec_at_const(const turbo_vec_t *vec, size_t index);
TURBO_STL_API void *turbo_vec_data(turbo_vec_t *vec);
TURBO_STL_API const void *turbo_vec_data_const(const turbo_vec_t *vec);
TURBO_STL_API size_t turbo_vec_size(const turbo_vec_t *vec);
TURBO_STL_API size_t turbo_vec_capacity(const turbo_vec_t *vec);
TURBO_STL_API uint64_t turbo_vec_generation(const turbo_vec_t *vec);
TURBO_STL_API bool turbo_vec_empty(const turbo_vec_t *vec);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_VEC_H */
