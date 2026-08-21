#ifndef TURBO_HEAP_H
#define TURBO_HEAP_H

#include "platform.h"
#include "turbo_error.h"

#include <stdbool.h>
#include <stddef.h>
#include "turbo_container_meta.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*turbo_heap_compare_fn)(const void *left, const void *right, void *ctx);

typedef struct {
  void *data;
  size_t size;
  size_t capacity;
  size_t elem_size;
  turbo_heap_compare_fn compare;
  void *compare_ctx;
} turbo_heap_t;

CXX_C_API int turbo_heap_init(turbo_heap_t *heap, size_t elem_size,
                              turbo_heap_compare_fn compare, void *compare_ctx);
/** Initialize a heap by copying and heapifying count contiguous elements. */
CXX_C_API int turbo_heap_from_array(turbo_heap_t *heap, const void *elements, size_t count,
                                    size_t elem_size, turbo_heap_compare_fn compare,
                                    void *compare_ctx);
CXX_C_API void turbo_heap_destroy(turbo_heap_t *heap);
CXX_C_API void turbo_heap_clear(turbo_heap_t *heap);
CXX_C_API int turbo_heap_reserve(turbo_heap_t *heap, size_t min_capacity);
CXX_C_API int turbo_heap_push(turbo_heap_t *heap, const void *elem);
CXX_C_API int turbo_heap_pop(turbo_heap_t *heap, void *out_elem);
CXX_C_API const void *turbo_heap_peek(const turbo_heap_t *heap);
static inline const void *turbo_heap_at_const(const turbo_heap_t *heap, size_t index) {
  if (heap == NULL || index >= heap->size) return NULL;
  return (const unsigned char *)heap->data + index * heap->elem_size;
}
CXX_C_API size_t turbo_heap_size(const turbo_heap_t *heap);
CXX_C_API size_t turbo_heap_capacity(const turbo_heap_t *heap);
CXX_C_API bool turbo_heap_empty(const turbo_heap_t *heap);

#define TURBO_HEAP_DEFINE(name, type, compare_fn) \
  CMETA_CONTAINER1_DEFINE(name, type, turbo_heap_t, turbo_heap, TURBO_OK, compare_fn, TURBO_META_HEAP_METHODS) \
  CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name, type, turbo_heap, \
      CMETA_RANGE_SIZED | CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE)

#ifdef __cplusplus
}
#endif

#endif /* TURBO_HEAP_H */
