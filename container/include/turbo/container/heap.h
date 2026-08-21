#ifndef TURBO_HEAP_H
#define TURBO_HEAP_H

#include <turbo/container/export.h>
#include <turbo/container/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <turbo/container/meta.h>

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

/* Borrowed pointers from peek/at become invalid after any successful mutation,
 * storage-changing reserve, clear, or destroy. */
CONTAINER_API container_status turbo_heap_init(turbo_heap_t *heap,
                                               const cmeta_type_desc *element_type,
                                               size_t element_limit);
CONTAINER_API container_status turbo_heap_init_bytes(turbo_heap_t *heap, size_t elem_size,
                                                     size_t elem_align, size_t element_limit,
                                                     turbo_heap_compare_fn compare,
                                                     void *compare_ctx);
CONTAINER_API container_status turbo_heap_from_array(turbo_heap_t *heap,
                                                     const void *elements, size_t count,
                                                     const cmeta_type_desc *element_type,
                                                     size_t element_limit);
CONTAINER_API container_status turbo_heap_from_array_bytes(turbo_heap_t *heap,
                                                           const void *elements, size_t count,
                                                           size_t elem_size, size_t elem_align,
                                                           size_t element_limit,
                                                           turbo_heap_compare_fn compare,
                                                           void *compare_ctx);
CONTAINER_API void turbo_heap_destroy(turbo_heap_t *heap);
CONTAINER_API container_status turbo_heap_clear(turbo_heap_t *heap);
CONTAINER_API container_status turbo_heap_reserve(turbo_heap_t *heap, size_t min_capacity);
CONTAINER_API container_status turbo_heap_push(turbo_heap_t *heap, const void *elem);
CONTAINER_API container_status turbo_heap_pop(turbo_heap_t *heap, void *out_elem);
CONTAINER_API const void *turbo_heap_peek(const turbo_heap_t *heap);
static inline const void *turbo_heap_at_const(const turbo_heap_t *heap, size_t index) {
  if (heap == NULL || index >= heap->size) return NULL;
  return (const unsigned char *)heap->data + index * heap->elem_stride;
}
CONTAINER_API size_t turbo_heap_size(const turbo_heap_t *heap);
CONTAINER_API size_t turbo_heap_capacity(const turbo_heap_t *heap);
CONTAINER_API uint64_t turbo_heap_generation(const turbo_heap_t *heap);
CONTAINER_API bool turbo_heap_empty(const turbo_heap_t *heap);

#define TURBO_HEAP_DEFINE(name, type, compare_fn) \
  CMETA_CONTAINER1_DEFINE(name, type, turbo_heap_t, turbo_heap, CONTAINER_OK, compare_fn, TURBO_META_HEAP_METHODS) \
  CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name, type, turbo_heap, \
      CMETA_RANGE_SIZED | CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE)

#ifdef __cplusplus
}
#endif

#endif /* TURBO_HEAP_H */
