#ifndef TURBO_HEAP_H
#define TURBO_HEAP_H

#include <turbostl/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <cmeta/cmeta.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*heap_compare_fn)(const void *left, const void *right, void *ctx);

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
  heap_compare_fn compare;
  void *compare_ctx;
} heap_t;

/* Handles must be first initialized with `{0}`. A destroyed handle may be
 * reused. init/from_array on a live handle return TURBO_STL_INVALID_ARGUMENT
 * without mutation. Borrowed pointers from peek/at become invalid after any
 * successful mutation, storage-changing reserve, clear, or destroy. */
turbostl_status heap_init(heap_t *heap,
                                               const cmeta_type_desc *element_type,
                                               size_t element_limit);
turbostl_status heap_init_bytes(heap_t *heap, size_t elem_size,
                                                     size_t elem_align, size_t element_limit,
                                                     heap_compare_fn compare,
                                                     void *compare_ctx);
turbostl_status heap_from_array(heap_t *heap,
                                                     const void *elements, size_t count,
                                                     const cmeta_type_desc *element_type,
                                                     size_t element_limit);
turbostl_status heap_from_array_bytes(heap_t *heap,
                                                           const void *elements, size_t count,
                                                           size_t elem_size, size_t elem_align,
                                                           size_t element_limit,
                                                           heap_compare_fn compare,
                                                           void *compare_ctx);
void heap_destroy(heap_t *heap);
turbostl_status heap_clear(heap_t *heap);
turbostl_status heap_reserve(heap_t *heap, size_t min_capacity);
turbostl_status heap_push(heap_t *heap, const void *elem);
/* A non-NULL out_elem must be sufficiently aligned, uninitialized element
 * storage; success transfers ownership there. NULL destroys the value. On
 * failure out_elem is not written. */
turbostl_status heap_pop(heap_t *heap, void *out_elem);
const void *heap_peek(const heap_t *heap);
static inline const void *heap_at_const(const heap_t *heap, size_t index) {
  if (heap == NULL || index >= heap->size) return NULL;
  return (const unsigned char *)heap->data + index * heap->elem_stride;
}
size_t heap_size(const heap_t *heap);
size_t heap_capacity(const heap_t *heap);
uint64_t heap_generation(const heap_t *heap);
bool heap_empty(const heap_t *heap);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_HEAP_H */
