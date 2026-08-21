#ifndef TURBO_HEAP_H
#define TURBO_HEAP_H

#include <turbo/stl/export.h>
#include <turbo/stl/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <cmeta/cmeta.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*turbo_heap_compare_fn)(const void *left, const void *right, void *ctx);

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
  turbo_heap_compare_fn compare;
  void *compare_ctx;
} turbo_heap_t;

/* Handles must be first initialized with `{0}`. A destroyed handle may be
 * reused. init/from_array on a live handle return TURBO_STL_INVALID_ARGUMENT
 * without mutation. Borrowed pointers from peek/at become invalid after any
 * successful mutation, storage-changing reserve, clear, or destroy. */
TURBO_STL_API turbo_stl_status turbo_heap_init(turbo_heap_t *heap,
                                               const cmeta_type_desc *element_type,
                                               size_t element_limit);
TURBO_STL_API turbo_stl_status turbo_heap_init_bytes(turbo_heap_t *heap, size_t elem_size,
                                                     size_t elem_align, size_t element_limit,
                                                     turbo_heap_compare_fn compare,
                                                     void *compare_ctx);
TURBO_STL_API turbo_stl_status turbo_heap_from_array(turbo_heap_t *heap,
                                                     const void *elements, size_t count,
                                                     const cmeta_type_desc *element_type,
                                                     size_t element_limit);
TURBO_STL_API turbo_stl_status turbo_heap_from_array_bytes(turbo_heap_t *heap,
                                                           const void *elements, size_t count,
                                                           size_t elem_size, size_t elem_align,
                                                           size_t element_limit,
                                                           turbo_heap_compare_fn compare,
                                                           void *compare_ctx);
TURBO_STL_API void turbo_heap_destroy(turbo_heap_t *heap);
TURBO_STL_API turbo_stl_status turbo_heap_clear(turbo_heap_t *heap);
TURBO_STL_API turbo_stl_status turbo_heap_reserve(turbo_heap_t *heap, size_t min_capacity);
TURBO_STL_API turbo_stl_status turbo_heap_push(turbo_heap_t *heap, const void *elem);
/* A non-NULL out_elem must be sufficiently aligned, uninitialized element
 * storage; success transfers ownership there. NULL destroys the value. On
 * failure out_elem is not written. */
TURBO_STL_API turbo_stl_status turbo_heap_pop(turbo_heap_t *heap, void *out_elem);
TURBO_STL_API const void *turbo_heap_peek(const turbo_heap_t *heap);
static inline const void *turbo_heap_at_const(const turbo_heap_t *heap, size_t index) {
  if (heap == NULL || index >= heap->size) return NULL;
  return (const unsigned char *)heap->data + index * heap->elem_stride;
}
TURBO_STL_API size_t turbo_heap_size(const turbo_heap_t *heap);
TURBO_STL_API size_t turbo_heap_capacity(const turbo_heap_t *heap);
TURBO_STL_API uint64_t turbo_heap_generation(const turbo_heap_t *heap);
TURBO_STL_API bool turbo_heap_empty(const turbo_heap_t *heap);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_HEAP_H */
