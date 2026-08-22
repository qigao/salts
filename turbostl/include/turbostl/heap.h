#ifndef TURBOSTL_HEAP_H
#define TURBOSTL_HEAP_H

#include <turbostl/status.h>

#include <cmeta/cmeta.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*heap_compare_fn)(const void *left, const void *right,
                               void *context);

typedef struct heap {
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

/* Typed heap ordering comes from element_type->traits->compare. */
stl_status heap_init(heap_t *heap, const cmeta_type_desc *element_type,
                     size_t element_limit);
stl_status heap_from_array(heap_t *heap, const void *elements, size_t count,
                           const cmeta_type_desc *element_type,
                           size_t element_limit);

/* Raw byte heaps require an explicit comparator/context. */
stl_status heap_init_bytes(heap_t *heap, size_t elem_size, size_t elem_align,
                           size_t element_limit, heap_compare_fn compare,
                           void *compare_ctx);
stl_status heap_from_array_bytes(heap_t *heap, const void *elements,
                                 size_t count, size_t elem_size,
                                 size_t elem_align, size_t element_limit,
                                 heap_compare_fn compare, void *compare_ctx);
void heap_destroy(heap_t *heap);
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

/* Temporary repository-migration aliases. */
typedef heap_compare_fn turbo_heap_compare_fn;
typedef heap_t turbo_heap_t;
#define turbo_heap_init heap_init
#define turbo_heap_init_bytes heap_init_bytes
#define turbo_heap_from_array heap_from_array
#define turbo_heap_from_array_bytes heap_from_array_bytes
#define turbo_heap_destroy heap_destroy
#define turbo_heap_clear heap_clear
#define turbo_heap_reserve heap_reserve
#define turbo_heap_push heap_push
#define turbo_heap_pop heap_pop
#define turbo_heap_peek heap_peek
#define turbo_heap_at_const heap_at_const
#define turbo_heap_size heap_size
#define turbo_heap_capacity heap_capacity
#define turbo_heap_generation heap_generation
#define turbo_heap_empty heap_empty

#ifdef __cplusplus
}
#endif

#endif /* TURBOSTL_HEAP_H */
