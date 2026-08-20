/**
 * @file vec.h
 * @brief Dynamic array implementation
 * 
 * THREAD SAFETY: NOT thread-safe. Each container_vec_t instance must be accessed
 *                by only one thread at a time. Use external synchronization
 *                (e.g., mutex) for shared vectors.
 * 
 * CONCURRENCY MODEL: Single-owner model. For multi-threaded scenarios:
 *                    - Use one vec per thread (thread-local), or
 *                    - Protect shared vec with external mutex, or
 *                    - Use read-write lock for concurrent reads + exclusive writes
 * 
 * CONCURRENT READS: Multiple threads may safely read a const container_vec_t
 *                   (using container_vec_at_const, container_vec_size, etc.) if no
 *                   thread is modifying it. Reallocation invalidates pointers.
 */

#ifndef CONTAINER_VEC_H
#define CONTAINER_VEC_H

#include "platform.h"
#include "turbo_error.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void *data;
  size_t size;
  size_t capacity;
  size_t elem_size;
} container_vec_t;

CXX_C_API int container_vec_init(container_vec_t *vec, size_t elem_size);
/** Initialize vec by copying count elements from a contiguous array. */
CXX_C_API int container_vec_from_array(container_vec_t *vec, const void *elements, size_t count,
                                   size_t elem_size);
CXX_C_API void container_vec_destroy(container_vec_t *vec);
CXX_C_API void container_vec_clear(container_vec_t *vec);
CXX_C_API int container_vec_reserve(container_vec_t *vec, size_t min_capacity);
CXX_C_API int container_vec_resize(container_vec_t *vec, size_t new_size);
CXX_C_API int container_vec_push(container_vec_t *vec, const void *elem);
CXX_C_API int container_vec_pop(container_vec_t *vec, void *out_elem);
CXX_C_API int container_vec_insert(container_vec_t *vec, size_t index, const void *elem);
CXX_C_API int container_vec_erase(container_vec_t *vec, size_t index, void *out_elem);
CXX_C_API int container_vec_swap_remove(container_vec_t *vec, size_t index, void *out_elem);
CXX_C_API void *container_vec_at(container_vec_t *vec, size_t index);
CXX_C_API const void *container_vec_at_const(const container_vec_t *vec, size_t index);
CXX_C_API void *container_vec_data(container_vec_t *vec);
CXX_C_API const void *container_vec_data_const(const container_vec_t *vec);
CXX_C_API size_t container_vec_size(const container_vec_t *vec);
CXX_C_API size_t container_vec_capacity(const container_vec_t *vec);
CXX_C_API bool container_vec_empty(const container_vec_t *vec);


#ifdef __cplusplus
}
#endif

#endif /* CONTAINER_VEC_H */
