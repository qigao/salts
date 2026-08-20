#include <container/heap.h>

#include <stdlib.h>
#include <string.h>

#define CONTAINER_HEAP_MIN_CAPACITY 8U

static int container_heap_valid(const container_heap_t *heap) {
  return heap != NULL && heap->elem_size > 0 && heap->compare != NULL;
}

static unsigned char *container_heap_elem(container_heap_t *heap, size_t index) {
  return (unsigned char *)heap->data + index * heap->elem_size;
}

static const unsigned char *container_heap_elem_const(const container_heap_t *heap, size_t index) {
  return (const unsigned char *)heap->data + index * heap->elem_size;
}

static int container_heap_less(const container_heap_t *heap, size_t left, size_t right) {
  return heap->compare(container_heap_elem_const(heap, left), container_heap_elem_const(heap, right),
                       heap->compare_ctx) < 0;
}

static int container_heap_grow_to(container_heap_t *heap, size_t min_capacity) {
  size_t new_capacity;
  void *new_data;

  if (!container_heap_valid(heap)) return TURBO_EINVAL;
  if (min_capacity <= heap->capacity) return TURBO_OK;
  if (min_capacity > SIZE_MAX / heap->elem_size) return TURBO_ENOMEM;

  new_capacity = heap->capacity ? heap->capacity : CONTAINER_HEAP_MIN_CAPACITY;
  while (new_capacity < min_capacity) {
    if (new_capacity > SIZE_MAX / 2U) {
      new_capacity = min_capacity;
      break;
    }
    new_capacity *= 2U;
  }
  if (new_capacity > SIZE_MAX / heap->elem_size) return TURBO_ENOMEM;

  new_data = realloc(heap->data, new_capacity * heap->elem_size);
  if (!new_data) return TURBO_ENOMEM;
  heap->data = new_data;
  heap->capacity = new_capacity;
  return TURBO_OK;
}

static int container_heap_swap(container_heap_t *heap, size_t a, size_t b) {
  unsigned char stack_tmp[256];
  unsigned char *left;
  unsigned char *right;
  size_t offset = 0;

  if (a == b) return TURBO_OK;
  left = container_heap_elem(heap, a);
  right = container_heap_elem(heap, b);
  while (offset < heap->elem_size) {
    size_t remaining = heap->elem_size - offset;
    size_t chunk_size = remaining < sizeof(stack_tmp) ? remaining : sizeof(stack_tmp);
    memcpy(stack_tmp, left + offset, chunk_size);
    memcpy(left + offset, right + offset, chunk_size);
    memcpy(right + offset, stack_tmp, chunk_size);
    offset += chunk_size;
  }
  return TURBO_OK;
}

static int container_heap_sift_up(container_heap_t *heap, size_t index) {
  while (index > 0) {
    size_t parent = (index - 1U) / 2U;
    int rc;
    if (!container_heap_less(heap, index, parent)) break;
    rc = container_heap_swap(heap, index, parent);
    if (rc != TURBO_OK) return rc;
    index = parent;
  }
  return TURBO_OK;
}

static int container_heap_sift_down(container_heap_t *heap, size_t index) {
  for (;;) {
    size_t left = index * 2U + 1U;
    size_t right = left + 1U;
    size_t best = index;
    int rc;

    if (left < heap->size && container_heap_less(heap, left, best)) best = left;
    if (right < heap->size && container_heap_less(heap, right, best)) best = right;
    if (best == index) break;
    rc = container_heap_swap(heap, index, best);
    if (rc != TURBO_OK) return rc;
    index = best;
  }
  return TURBO_OK;
}

int container_heap_init(container_heap_t *heap, size_t elem_size, container_heap_compare_fn compare,
                    void *compare_ctx) {
  if (!heap || elem_size == 0 || !compare) return TURBO_EINVAL;
  memset(heap, 0, sizeof(*heap));
  heap->elem_size = elem_size;
  heap->compare = compare;
  heap->compare_ctx = compare_ctx;
  return TURBO_OK;
}

int container_heap_from_array(container_heap_t *heap, const void *elements, size_t count,
                          size_t elem_size, container_heap_compare_fn compare,
                          void *compare_ctx) {
  size_t parent;
  int rc;

  if (!heap || elem_size == 0 || !compare || (count > 0 && !elements)) return TURBO_EINVAL;
  rc = container_heap_init(heap, elem_size, compare, compare_ctx);
  if (rc != TURBO_OK) return rc;
  rc = container_heap_reserve(heap, count);
  if (rc != TURBO_OK) {
    container_heap_destroy(heap);
    return rc;
  }
  if (count > 0) memcpy(heap->data, elements, count * elem_size);
  heap->size = count;
  for (parent = count / 2U; parent > 0; --parent) {
    rc = container_heap_sift_down(heap, parent - 1U);
    if (rc != TURBO_OK) {
      container_heap_destroy(heap);
      return rc;
    }
  }
  return TURBO_OK;
}

void container_heap_destroy(container_heap_t *heap) {
  if (!heap) return;
  free(heap->data);
  memset(heap, 0, sizeof(*heap));
}

void container_heap_clear(container_heap_t *heap) {
  if (!heap) return;
  heap->size = 0;
}

int container_heap_reserve(container_heap_t *heap, size_t min_capacity) {
  return container_heap_grow_to(heap, min_capacity);
}

int container_heap_push(container_heap_t *heap, const void *elem) {
  int rc;

  if (!container_heap_valid(heap) || !elem) return TURBO_EINVAL;
  if (heap->size == SIZE_MAX) return TURBO_ENOMEM;
  rc = container_heap_grow_to(heap, heap->size + 1U);
  if (rc != TURBO_OK) return rc;
  memcpy(container_heap_elem(heap, heap->size), elem, heap->elem_size);
  heap->size += 1U;
  return container_heap_sift_up(heap, heap->size - 1U);
}

int container_heap_pop(container_heap_t *heap, void *out_elem) {
  if (!container_heap_valid(heap)) return TURBO_EINVAL;
  if (heap->size == 0) return TURBO_ENOENT;

  if (out_elem) memcpy(out_elem, heap->data, heap->elem_size);
  heap->size -= 1U;
  if (heap->size > 0) {
    memcpy(heap->data, container_heap_elem(heap, heap->size), heap->elem_size);
    return container_heap_sift_down(heap, 0);
  }
  return TURBO_OK;
}

const void *container_heap_peek(const container_heap_t *heap) {
  if (!container_heap_valid(heap) || heap->size == 0) return NULL;
  return heap->data;
}

size_t container_heap_size(const container_heap_t *heap) {
  if (!heap) return 0;
  return heap->size;
}

size_t container_heap_capacity(const container_heap_t *heap) {
  if (!heap) return 0;
  return heap->capacity;
}

bool container_heap_empty(const container_heap_t *heap) {
  return heap == NULL || heap->size == 0;
}
