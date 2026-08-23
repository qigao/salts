#ifndef TURBOSTL_HEAP_H
#define TURBOSTL_HEAP_H

#include <turbostl/status.h>

#include <cmeta/range.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*heap_compare_fn)(const void *left, const void *right,
                               void *context);

typedef struct heap {
  cmeta_container_header cmeta;
  void *data;
  size_t size;
  size_t capacity;
  size_t elem_size;
  size_t elem_stride;
  size_t elem_align;
  size_t element_limit;
  const cmeta_type_desc *element_type;
  heap_compare_fn compare;
  void *compare_ctx;
  uint64_t generation;
  bool initialized;
} heap_t;

/* Storage bridge shared by generated facades and explicitly bound raw handles. */
stl_status heap_raw_init(heap_t *heap,
                         const cmeta_type_desc *element_type,
                         size_t element_limit);
stl_status heap_raw_from_array(heap_t *heap, const void *elements,
                               size_t count,
                               const cmeta_type_desc *element_type,
                               size_t element_limit);
void heap_raw_destroy_storage(heap_t *heap);

/* Raw byte heaps require an explicit comparator/context. */
stl_status heap_init_bytes(heap_t *heap, size_t elem_size, size_t elem_align,
                           size_t element_limit, heap_compare_fn compare,
                           void *compare_ctx);
stl_status heap_from_array_bytes(heap_t *heap, const void *elements,
                                 size_t count, size_t elem_size,
                                 size_t elem_align, size_t element_limit,
                                 heap_compare_fn compare, void *compare_ctx);

/* Typed heap ordering comes from the bound element_type->traits->compare. */
static inline stl_status heap_init(heap_t *heap, size_t element_limit) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *type;
  stl_status status;
  if (heap == NULL || heap->element_type == NULL)
    return STL_INVALID_ARGUMENT;
  kind = heap->cmeta.descriptor;
  type = heap->element_type;
  status = heap_raw_init(heap, type, element_limit);
  heap->cmeta.descriptor = kind;
  heap->element_type = type;
  return status;
}

static inline stl_status heap_from_array(heap_t *heap, const void *elements,
                                         size_t count,
                                         size_t element_limit) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *type;
  stl_status status;
  if (heap == NULL || heap->element_type == NULL)
    return STL_INVALID_ARGUMENT;
  kind = heap->cmeta.descriptor;
  type = heap->element_type;
  status = heap_raw_from_array(heap, elements, count, type, element_limit);
  heap->cmeta.descriptor = kind;
  heap->element_type = type;
  return status;
}

static inline void heap_destroy(heap_t *heap) {
  const cmeta_container_desc *kind;
  const cmeta_type_desc *type;
  if (heap == NULL)
    return;
  kind = heap->cmeta.descriptor;
  type = heap->element_type;
  heap_raw_destroy_storage(heap);
  heap->cmeta.descriptor = kind;
  heap->element_type = type;
}

stl_status heap_clear(heap_t *heap);
stl_status heap_reserve(heap_t *heap, size_t min_capacity);
stl_status heap_push(heap_t *heap, const void *elem);
stl_status heap_pop(heap_t *heap, void *out_elem);
const void *heap_peek(const heap_t *heap);
static inline const void *heap_at_const(const heap_t *heap, size_t index) {
  if (heap == NULL || index >= heap->size)
    return NULL;
  return (const unsigned char *)heap->data + index * heap->elem_stride;
}
size_t heap_size(const heap_t *heap);
size_t heap_capacity(const heap_t *heap);
uint64_t heap_generation(const heap_t *heap);
bool heap_empty(const heap_t *heap);


#ifdef __cplusplus
}
#endif

#endif /* TURBOSTL_HEAP_H */
