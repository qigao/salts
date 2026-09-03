/**
 * @file vec.h
 * @brief Dynamic array implementation.
 *
 * THREAD SAFETY: NOT thread-safe. Each vec_t instance must be accessed by only
 * one thread at a time. Use external synchronization for shared vectors.
 */

#ifndef CSTL_VEC_H
#define CSTL_VEC_H

#include <cstl/status.h>

#include <cmeta/cmeta.h>
#include <cmeta/range.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vec {
  cmeta_container_header cmeta;
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

/* Storage bridge shared by generated facades and explicitly bound raw handles. */
stl_status vec_raw_init(vec_t *vec, const cmeta_type_desc *element_type,
                        size_t element_limit);
stl_status vec_raw_from_array(vec_t *vec, const void *elements, size_t count,
                              const cmeta_type_desc *element_type,
                              size_t element_limit);
void vec_raw_destroy_storage(vec_t *vec);

/* Raw byte entry points remain explicit. */
stl_status vec_init_bytes(vec_t *vec, size_t elem_size, size_t elem_align,
                          size_t element_limit);
stl_status vec_from_array_bytes(vec_t *vec, const void *elements, size_t count,
                                size_t elem_size, size_t elem_align,
                                size_t element_limit);

/* Self-describing typed entry points consume the binding already in the
 * handle. The wrapper preserves declaration metadata across implementation
 * reset paths while the compiled implementation is being migrated. */
static inline stl_status vec_init(vec_t *vec, size_t element_limit) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *type;
  stl_status status;
  if (vec == NULL || vec->element_type == NULL)
    return STL_INVALID_ARGUMENT;
  kind = vec->cmeta.descriptor;
  type = vec->element_type;
  status = vec_raw_init(vec, type, element_limit);
  vec->cmeta.descriptor = kind;
  if (status != STL_OK)
    vec->element_type = type;
  return status;
}

static inline stl_status vec_from_array(vec_t *vec, const void *elements,
                                        size_t count, size_t element_limit) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *type;
  stl_status status;
  if (vec == NULL || vec->element_type == NULL)
    return STL_INVALID_ARGUMENT;
  kind = vec->cmeta.descriptor;
  type = vec->element_type;
  status = vec_raw_from_array(vec, elements, count, type, element_limit);
  vec->cmeta.descriptor = kind;
  if (status != STL_OK)
    vec->element_type = type;
  return status;
}

static inline void vec_destroy(vec_t *vec) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *type;
  if (vec == NULL)
    return;
  kind = vec->cmeta.descriptor;
  type = vec->element_type;
  vec_raw_destroy_storage(vec);
  vec->cmeta.descriptor = kind;
  vec->element_type = type;
}

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


#ifdef __cplusplus
}
#endif

#endif /* CSTL_VEC_H */
