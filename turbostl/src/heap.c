#include <turbostl/heap.h>

#include "sequence_internal.h"

#include <string.h>

#define TURBO_HEAP_MIN_CAPACITY 8u

static bool heap_valid(const heap_t *heap) {
    return heap != NULL && heap->initialized && heap->elem_size != 0u &&
           (heap->element_type != NULL || heap->compare != NULL);
}
static unsigned char *heap_slot(heap_t *heap, size_t index) {
    return (unsigned char *)heap->data + index * heap->elem_stride;
}
static const unsigned char *heap_slot_const(const heap_t *heap, size_t index) {
    return (const unsigned char *)heap->data + index * heap->elem_stride;
}
static int heap_compare_values(const heap_t *heap, size_t left, size_t right) {
    const void *a = heap_slot_const(heap, left);
    const void *b = heap_slot_const(heap, right);
    return heap->element_type ? heap->element_type->traits->compare(a, b)
                              : heap->compare(a, b, heap->compare_ctx);
}
static bool heap_less(const heap_t *heap, size_t left, size_t right) {
    return heap_compare_values(heap, left, right) < 0;
}

static turbostl_status heap_next_capacity(const heap_t *heap, size_t minimum,
                                                 size_t *out_capacity) {
    size_t capacity;
    if (minimum > heap->element_limit) return TURBO_STL_CAPACITY_EXCEEDED;
    capacity = heap->capacity ? heap->capacity : TURBO_HEAP_MIN_CAPACITY;
    if (capacity > heap->element_limit) capacity = heap->element_limit;
    while (capacity < minimum) {
        if (capacity > heap->element_limit - capacity) { capacity = heap->element_limit; break; }
        capacity *= 2u;
    }
    *out_capacity = capacity;
    return TURBO_STL_OK;
}
static turbostl_status heap_grow_to(heap_t *heap, size_t minimum, bool *changed) {
    void *data;
    size_t capacity, index;
    turbostl_status status;
    if (changed) *changed = false;
    if (!heap_valid(heap)) return TURBO_STL_INVALID_ARGUMENT;
    if (minimum <= heap->capacity) return TURBO_STL_OK;
    status = heap_next_capacity(heap, minimum, &capacity);
    if (status != TURBO_STL_OK) return status;
    status = sequence_allocate(capacity, heap->elem_stride, heap->elem_align, &data);
    if (status != TURBO_STL_OK) return status;
    if (!heap->element_type) {
        if (heap->size) memcpy(data, heap->data, heap->size * heap->elem_stride);
    } else {
        for (index = 0u; index < heap->size; ++index) {
            status = sequence_move_destroy(heap->element_type, heap->elem_size,
                (unsigned char *)data + index * heap->elem_stride, heap_slot(heap, index));
            if (status != TURBO_STL_OK) { sequence_deallocate(data); return status; }
        }
    }
    sequence_deallocate(heap->data);
    heap->data = data; heap->capacity = capacity;
    if (changed) *changed = true;
    return TURBO_STL_OK;
}
static turbostl_status heap_prepare_copy(const heap_t *heap, const void *elem,
                                                void **out_value) {
    turbostl_status status;
    if (!elem || !out_value) return TURBO_STL_INVALID_ARGUMENT;
    status = sequence_allocate(1u, heap->elem_stride, heap->elem_align, out_value);
    if (status != TURBO_STL_OK) return status;
    status = sequence_copy(heap->element_type, heap->elem_size, *out_value, elem);
    if (status != TURBO_STL_OK) { sequence_deallocate(*out_value); *out_value = NULL; }
    return status;
}
static void heap_discard_prepared(const heap_t *heap, void *value) {
    if (value) { (void)sequence_destroy_value(heap->element_type, value); sequence_deallocate(value); }
}
static void heap_swap(heap_t *heap, size_t left, size_t right, void *scratch) {
    if (left == right) return;
    if (!heap->element_type) {
        memcpy(scratch, heap_slot(heap, left), heap->elem_size);
        memcpy(heap_slot(heap, left), heap_slot(heap, right), heap->elem_size);
        memcpy(heap_slot(heap, right), scratch, heap->elem_size);
        return;
    }
    (void)sequence_move_destroy(heap->element_type, heap->elem_size, scratch,
                                      heap_slot(heap, left));
    (void)sequence_move_destroy(heap->element_type, heap->elem_size,
                                      heap_slot(heap, left), heap_slot(heap, right));
    (void)sequence_move_destroy(heap->element_type, heap->elem_size,
                                      heap_slot(heap, right), scratch);
}
static void heap_sift_up(heap_t *heap, size_t index, void *scratch) {
    while (index) {
        size_t parent = (index - 1u) / 2u;
        if (!heap_less(heap, index, parent)) break;
        heap_swap(heap, index, parent, scratch);
        index = parent;
    }
}
static void heap_sift_down(heap_t *heap, size_t index, void *scratch) {
    for (;;) {
        if (index > (SIZE_MAX - 1u) / 2u) break;
        size_t left = index * 2u + 1u;
        size_t right = left + 1u;
        size_t best = index;
        if (left < heap->size && heap_less(heap, left, best)) best = left;
        if (right < heap->size && heap_less(heap, right, best)) best = right;
        if (best == index) break;
        heap_swap(heap, index, best, scratch);
        index = best;
    }
}

static turbostl_status heap_initialize(heap_t *heap, const cmeta_type_desc *type,
                                              size_t size, size_t align, size_t limit,
                                              heap_compare_fn compare, void *context) {
    turbostl_status status;
    size_t stride;
    uint64_t generation;
    if (!heap || (!type && !compare)) return TURBO_STL_INVALID_ARGUMENT;
    if (heap->initialized) return TURBO_STL_INVALID_ARGUMENT;
    status = sequence_stride(size, align, &stride);
    if (status != TURBO_STL_OK) return status;
    generation = heap->generation + UINT64_C(1);
    memset(heap, 0, sizeof(*heap));
    heap->elem_size = size; heap->elem_stride = stride; heap->elem_align = align;
    heap->element_limit = limit; heap->element_type = type; heap->compare = compare;
    heap->compare_ctx = context; heap->generation = generation; heap->initialized = true;
    return TURBO_STL_OK;
}
turbostl_status heap_init_bytes(heap_t *heap, size_t elem_size, size_t elem_align,
                                       size_t element_limit, heap_compare_fn compare,
                                       void *compare_ctx) {
    return heap_initialize(heap, NULL, elem_size, elem_align, element_limit, compare, compare_ctx);
}
turbostl_status heap_init(heap_t *heap, const cmeta_type_desc *element_type,
                                 size_t element_limit) {
    turbostl_status status;
    if (!heap || heap->initialized) return TURBO_STL_INVALID_ARGUMENT;
    status = sequence_require_type(element_type, true);
    if (status != TURBO_STL_OK) return status;
    return heap_initialize(heap, element_type, element_type->size, element_type->align,
                                 element_limit, NULL, NULL);
}
turbostl_status heap_from_array_bytes(heap_t *heap, const void *elements, size_t count,
                                             size_t elem_size, size_t elem_align, size_t limit,
                                             heap_compare_fn compare, void *context) {
    heap_t temporary = {0};
    turbostl_status status;
    size_t index;
    uint64_t generation;
    if (!heap) return TURBO_STL_INVALID_ARGUMENT;
    if (heap->initialized) return TURBO_STL_INVALID_ARGUMENT;
    if ((count && !elements) || count > limit)
        return count > limit ? TURBO_STL_CAPACITY_EXCEEDED : TURBO_STL_INVALID_ARGUMENT;
    status = heap_init_bytes(&temporary, elem_size, elem_align, limit, compare, context);
    if (status != TURBO_STL_OK) return status;
    for (index = 0u; index < count; ++index) {
        status = heap_push(&temporary, (const unsigned char *)elements + index * elem_size);
        if (status != TURBO_STL_OK) { heap_destroy(&temporary); return status; }
    }
    generation = heap->generation + UINT64_C(1);
    temporary.generation = generation;
    *heap = temporary;
    return TURBO_STL_OK;
}
turbostl_status heap_from_array(heap_t *heap, const void *elements, size_t count,
                                       const cmeta_type_desc *type, size_t limit) {
    heap_t temporary = {0};
    turbostl_status status;
    size_t index;
    uint64_t generation;
    if (!heap) return TURBO_STL_INVALID_ARGUMENT;
    if (heap->initialized) return TURBO_STL_INVALID_ARGUMENT;
    if ((count && !elements) || count > limit)
        return count > limit ? TURBO_STL_CAPACITY_EXCEEDED : TURBO_STL_INVALID_ARGUMENT;
    status = heap_init(&temporary, type, limit);
    if (status != TURBO_STL_OK) return status;
    for (index = 0u; index < count; ++index) {
        status = heap_push(&temporary, (const unsigned char *)elements + index * type->size);
        if (status != TURBO_STL_OK) { heap_destroy(&temporary); return status; }
    }
    generation = heap->generation + UINT64_C(1);
    temporary.generation = generation;
    *heap = temporary;
    return TURBO_STL_OK;
}

void heap_destroy(heap_t *heap) {
    uint64_t generation;
    if (!heap) return;
    generation = heap->generation;
    if (heap->initialized) {
        (void)heap_clear(heap);
        generation = heap->generation + UINT64_C(1);
        sequence_deallocate(heap->data);
    }
    memset(heap, 0, sizeof(*heap));
    heap->generation = generation;
}
turbostl_status heap_clear(heap_t *heap) {
    size_t index;
    turbostl_status status;
    if (!heap_valid(heap)) return TURBO_STL_INVALID_ARGUMENT;
    for (index = 0u; index < heap->size; ++index) {
        status = sequence_destroy_value(heap->element_type, heap_slot(heap, index));
        if (status != TURBO_STL_OK) return status;
    }
    if (heap->size) { heap->size = 0u; ++heap->generation; }
    return TURBO_STL_OK;
}
turbostl_status heap_reserve(heap_t *heap, size_t min_capacity) {
    bool changed;
    turbostl_status status = heap_grow_to(heap, min_capacity, &changed);
    if (status == TURBO_STL_OK && changed) ++heap->generation;
    return status;
}
turbostl_status heap_push(heap_t *heap, const void *elem) {
    void *prepared = NULL;
    void *scratch = NULL;
    turbostl_status status;
    if (!heap_valid(heap) || !elem) return TURBO_STL_INVALID_ARGUMENT;
    if (heap->size >= heap->element_limit) return TURBO_STL_CAPACITY_EXCEEDED;
    status = heap_prepare_copy(heap, elem, &prepared);
    if (status != TURBO_STL_OK) return status;
    status = sequence_allocate(1u, heap->elem_stride, heap->elem_align, &scratch);
    if (status != TURBO_STL_OK) { heap_discard_prepared(heap, prepared); return status; }
    status = heap_grow_to(heap, heap->size + 1u, NULL);
    if (status != TURBO_STL_OK) { sequence_deallocate(scratch); heap_discard_prepared(heap, prepared); return status; }
    status = sequence_move_destroy(heap->element_type, heap->elem_size,
        heap_slot(heap, heap->size), prepared);
    sequence_deallocate(prepared);
    if (status != TURBO_STL_OK) { sequence_deallocate(scratch); return status; }
    ++heap->size;
    heap_sift_up(heap, heap->size - 1u, scratch);
    sequence_deallocate(scratch);
    ++heap->generation;
    return TURBO_STL_OK;
}
turbostl_status heap_pop(heap_t *heap, void *out_elem) {
    void *scratch = NULL;
    turbostl_status status;
    size_t last;
    if (!heap_valid(heap)) return TURBO_STL_INVALID_ARGUMENT;
    if (!heap->size) return TURBO_STL_EMPTY;
    if (heap->size > 1u) {
        status = sequence_allocate(1u, heap->elem_stride, heap->elem_align, &scratch);
        if (status != TURBO_STL_OK) return status;
    }
    status = out_elem ? sequence_move_destroy(heap->element_type, heap->elem_size, out_elem,
                                                     heap_slot(heap, 0u))
                     : sequence_destroy_value(heap->element_type, heap_slot(heap, 0u));
    if (status != TURBO_STL_OK) { sequence_deallocate(scratch); return status; }
    last = heap->size - 1u;
    if (last) {
        (void)sequence_move_destroy(heap->element_type, heap->elem_size,
            heap_slot(heap, 0u), heap_slot(heap, last));
    }
    heap->size = last;
    if (heap->size) heap_sift_down(heap, 0u, scratch);
    sequence_deallocate(scratch);
    ++heap->generation;
    return TURBO_STL_OK;
}

const void *heap_peek(const heap_t *heap) {
    return heap_valid(heap) && heap->size ? heap->data : NULL;
}
size_t heap_size(const heap_t *heap) { return heap_valid(heap) ? heap->size : 0u; }
size_t heap_capacity(const heap_t *heap) { return heap_valid(heap) ? heap->capacity : 0u; }
uint64_t heap_generation(const heap_t *heap) { return heap ? heap->generation : UINT64_C(0); }
bool heap_empty(const heap_t *heap) { return heap_size(heap) == 0u; }
