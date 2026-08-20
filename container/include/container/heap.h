#ifndef CONTAINER_HEAP_H
#define CONTAINER_HEAP_H

#include "platform.h"
#include "turbo_error.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*container_heap_compare_fn)(const void *left, const void *right, void *ctx);

typedef struct {
  void *data;
  size_t size;
  size_t capacity;
  size_t elem_size;
  container_heap_compare_fn compare;
  void *compare_ctx;
} container_heap_t;

CXX_C_API int container_heap_init(container_heap_t *heap, size_t elem_size,
                              container_heap_compare_fn compare, void *compare_ctx);
/** Initialize a heap by copying and heapifying count contiguous elements. */
CXX_C_API int container_heap_from_array(container_heap_t *heap, const void *elements, size_t count,
                                    size_t elem_size, container_heap_compare_fn compare,
                                    void *compare_ctx);
CXX_C_API void container_heap_destroy(container_heap_t *heap);
CXX_C_API void container_heap_clear(container_heap_t *heap);
CXX_C_API int container_heap_reserve(container_heap_t *heap, size_t min_capacity);
CXX_C_API int container_heap_push(container_heap_t *heap, const void *elem);
CXX_C_API int container_heap_pop(container_heap_t *heap, void *out_elem);
CXX_C_API const void *container_heap_peek(const container_heap_t *heap);
static inline const void *container_heap_at_const(const container_heap_t *heap, size_t index) {
  if (heap == NULL || index >= heap->size) return NULL;
  return (const unsigned char *)heap->data + index * heap->elem_size;
}
CXX_C_API size_t container_heap_size(const container_heap_t *heap);
CXX_C_API size_t container_heap_capacity(const container_heap_t *heap);
CXX_C_API bool container_heap_empty(const container_heap_t *heap);


#ifdef __cplusplus
}
#endif

#endif /* CONTAINER_HEAP_H */
