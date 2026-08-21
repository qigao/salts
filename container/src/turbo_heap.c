#include <turbo/container/heap.h>

#include "turbo_sequence_internal.h"

#include <string.h>

#define TURBO_HEAP_MIN_CAPACITY 8u

static bool turbo_heap_valid(const turbo_heap_t *heap) {
    return heap != NULL && heap->initialized && heap->elem_size != 0u &&
           (heap->element_type != NULL || heap->compare != NULL);
}
static unsigned char *turbo_heap_slot(turbo_heap_t *heap, size_t index) {
    return (unsigned char *)heap->data + index * heap->elem_stride;
}
static const unsigned char *turbo_heap_slot_const(const turbo_heap_t *heap, size_t index) {
    return (const unsigned char *)heap->data + index * heap->elem_stride;
}
static int turbo_heap_compare_values(const turbo_heap_t *heap, size_t left, size_t right) {
    const void *a = turbo_heap_slot_const(heap, left);
    const void *b = turbo_heap_slot_const(heap, right);
    return heap->element_type ? heap->element_type->traits->compare(a, b)
                              : heap->compare(a, b, heap->compare_ctx);
}
static bool turbo_heap_less(const turbo_heap_t *heap, size_t left, size_t right) {
    return turbo_heap_compare_values(heap, left, right) < 0;
}

static container_status turbo_heap_next_capacity(const turbo_heap_t *heap, size_t minimum,
                                                 size_t *out_capacity) {
    size_t capacity;
    if (minimum > heap->element_limit) return CONTAINER_CAPACITY_EXCEEDED;
    capacity = heap->capacity ? heap->capacity : TURBO_HEAP_MIN_CAPACITY;
    if (capacity > heap->element_limit) capacity = heap->element_limit;
    while (capacity < minimum) {
        if (capacity > heap->element_limit - capacity) { capacity = heap->element_limit; break; }
        capacity *= 2u;
    }
    *out_capacity = capacity;
    return CONTAINER_OK;
}
static container_status turbo_heap_grow_to(turbo_heap_t *heap, size_t minimum, bool *changed) {
    void *data;
    size_t capacity, index;
    container_status status;
    if (changed) *changed = false;
    if (!turbo_heap_valid(heap)) return CONTAINER_INVALID_ARGUMENT;
    if (minimum <= heap->capacity) return CONTAINER_OK;
    status = turbo_heap_next_capacity(heap, minimum, &capacity);
    if (status != CONTAINER_OK) return status;
    status = turbo_sequence_allocate(capacity, heap->elem_stride, heap->elem_align, &data);
    if (status != CONTAINER_OK) return status;
    if (!heap->element_type) {
        if (heap->size) memcpy(data, heap->data, heap->size * heap->elem_stride);
    } else {
        for (index = 0u; index < heap->size; ++index) {
            status = turbo_sequence_move_destroy(heap->element_type, heap->elem_size,
                (unsigned char *)data + index * heap->elem_stride, turbo_heap_slot(heap, index));
            if (status != CONTAINER_OK) { turbo_sequence_deallocate(data); return status; }
        }
    }
    turbo_sequence_deallocate(heap->data);
    heap->data = data; heap->capacity = capacity;
    if (changed) *changed = true;
    return CONTAINER_OK;
}
static container_status turbo_heap_prepare_copy(const turbo_heap_t *heap, const void *elem,
                                                void **out_value) {
    container_status status;
    if (!elem || !out_value) return CONTAINER_INVALID_ARGUMENT;
    status = turbo_sequence_allocate(1u, heap->elem_stride, heap->elem_align, out_value);
    if (status != CONTAINER_OK) return status;
    status = turbo_sequence_copy(heap->element_type, heap->elem_size, *out_value, elem);
    if (status != CONTAINER_OK) { turbo_sequence_deallocate(*out_value); *out_value = NULL; }
    return status;
}
static void turbo_heap_discard_prepared(const turbo_heap_t *heap, void *value) {
    if (value) { (void)turbo_sequence_destroy_value(heap->element_type, value); turbo_sequence_deallocate(value); }
}
static void turbo_heap_swap(turbo_heap_t *heap, size_t left, size_t right, void *scratch) {
    if (left == right) return;
    if (!heap->element_type) {
        memcpy(scratch, turbo_heap_slot(heap, left), heap->elem_size);
        memcpy(turbo_heap_slot(heap, left), turbo_heap_slot(heap, right), heap->elem_size);
        memcpy(turbo_heap_slot(heap, right), scratch, heap->elem_size);
        return;
    }
    (void)turbo_sequence_move_destroy(heap->element_type, heap->elem_size, scratch,
                                      turbo_heap_slot(heap, left));
    (void)turbo_sequence_move_destroy(heap->element_type, heap->elem_size,
                                      turbo_heap_slot(heap, left), turbo_heap_slot(heap, right));
    (void)turbo_sequence_move_destroy(heap->element_type, heap->elem_size,
                                      turbo_heap_slot(heap, right), scratch);
}
static void turbo_heap_sift_up(turbo_heap_t *heap, size_t index, void *scratch) {
    while (index) {
        size_t parent = (index - 1u) / 2u;
        if (!turbo_heap_less(heap, index, parent)) break;
        turbo_heap_swap(heap, index, parent, scratch);
        index = parent;
    }
}
static void turbo_heap_sift_down(turbo_heap_t *heap, size_t index, void *scratch) {
    for (;;) {
        if (index > (SIZE_MAX - 1u) / 2u) break;
        size_t left = index * 2u + 1u;
        size_t right = left + 1u;
        size_t best = index;
        if (left < heap->size && turbo_heap_less(heap, left, best)) best = left;
        if (right < heap->size && turbo_heap_less(heap, right, best)) best = right;
        if (best == index) break;
        turbo_heap_swap(heap, index, best, scratch);
        index = best;
    }
}

static container_status turbo_heap_initialize(turbo_heap_t *heap, const cmeta_type_desc *type,
                                              size_t size, size_t align, size_t limit,
                                              turbo_heap_compare_fn compare, void *context) {
    container_status status;
    size_t stride;
    uint64_t generation;
    if (!heap || (!type && !compare)) return CONTAINER_INVALID_ARGUMENT;
    if (heap->initialized) return CONTAINER_INVALID_ARGUMENT;
    status = turbo_sequence_stride(size, align, &stride);
    if (status != CONTAINER_OK) return status;
    generation = heap->generation + UINT64_C(1);
    memset(heap, 0, sizeof(*heap));
    heap->elem_size = size; heap->elem_stride = stride; heap->elem_align = align;
    heap->element_limit = limit; heap->element_type = type; heap->compare = compare;
    heap->compare_ctx = context; heap->generation = generation; heap->initialized = true;
    return CONTAINER_OK;
}
container_status turbo_heap_init_bytes(turbo_heap_t *heap, size_t elem_size, size_t elem_align,
                                       size_t element_limit, turbo_heap_compare_fn compare,
                                       void *compare_ctx) {
    return turbo_heap_initialize(heap, NULL, elem_size, elem_align, element_limit, compare, compare_ctx);
}
container_status turbo_heap_init(turbo_heap_t *heap, const cmeta_type_desc *element_type,
                                 size_t element_limit) {
    container_status status = turbo_sequence_require_type(element_type, true);
    if (status != CONTAINER_OK) return status;
    return turbo_heap_initialize(heap, element_type, element_type->size, element_type->align,
                                 element_limit, NULL, NULL);
}
container_status turbo_heap_from_array_bytes(turbo_heap_t *heap, const void *elements, size_t count,
                                             size_t elem_size, size_t elem_align, size_t limit,
                                             turbo_heap_compare_fn compare, void *context) {
    container_status status;
    size_t index;
    if ((count && !elements) || count > limit)
        return count > limit ? CONTAINER_CAPACITY_EXCEEDED : CONTAINER_INVALID_ARGUMENT;
    status = turbo_heap_init_bytes(heap, elem_size, elem_align, limit, compare, context);
    if (status != CONTAINER_OK) return status;
    for (index = 0u; index < count; ++index) {
        status = turbo_heap_push(heap, (const unsigned char *)elements + index * elem_size);
        if (status != CONTAINER_OK) { turbo_heap_destroy(heap); return status; }
    }
    return CONTAINER_OK;
}
container_status turbo_heap_from_array(turbo_heap_t *heap, const void *elements, size_t count,
                                       const cmeta_type_desc *type, size_t limit) {
    container_status status;
    size_t index;
    if ((count && !elements) || count > limit)
        return count > limit ? CONTAINER_CAPACITY_EXCEEDED : CONTAINER_INVALID_ARGUMENT;
    status = turbo_heap_init(heap, type, limit);
    if (status != CONTAINER_OK) return status;
    for (index = 0u; index < count; ++index) {
        status = turbo_heap_push(heap, (const unsigned char *)elements + index * type->size);
        if (status != CONTAINER_OK) { turbo_heap_destroy(heap); return status; }
    }
    return CONTAINER_OK;
}

void turbo_heap_destroy(turbo_heap_t *heap) {
    uint64_t generation;
    if (!heap) return;
    generation = heap->generation;
    if (heap->initialized) {
        (void)turbo_heap_clear(heap);
        generation = heap->generation + UINT64_C(1);
        turbo_sequence_deallocate(heap->data);
    }
    memset(heap, 0, sizeof(*heap));
    heap->generation = generation;
}
container_status turbo_heap_clear(turbo_heap_t *heap) {
    size_t index;
    container_status status;
    if (!turbo_heap_valid(heap)) return CONTAINER_INVALID_ARGUMENT;
    for (index = 0u; index < heap->size; ++index) {
        status = turbo_sequence_destroy_value(heap->element_type, turbo_heap_slot(heap, index));
        if (status != CONTAINER_OK) return status;
    }
    if (heap->size) { heap->size = 0u; ++heap->generation; }
    return CONTAINER_OK;
}
container_status turbo_heap_reserve(turbo_heap_t *heap, size_t min_capacity) {
    bool changed;
    container_status status = turbo_heap_grow_to(heap, min_capacity, &changed);
    if (status == CONTAINER_OK && changed) ++heap->generation;
    return status;
}
container_status turbo_heap_push(turbo_heap_t *heap, const void *elem) {
    void *prepared = NULL;
    void *scratch = NULL;
    container_status status;
    if (!turbo_heap_valid(heap) || !elem) return CONTAINER_INVALID_ARGUMENT;
    if (heap->size >= heap->element_limit) return CONTAINER_CAPACITY_EXCEEDED;
    status = turbo_heap_prepare_copy(heap, elem, &prepared);
    if (status != CONTAINER_OK) return status;
    status = turbo_sequence_allocate(1u, heap->elem_stride, heap->elem_align, &scratch);
    if (status != CONTAINER_OK) { turbo_heap_discard_prepared(heap, prepared); return status; }
    status = turbo_heap_grow_to(heap, heap->size + 1u, NULL);
    if (status != CONTAINER_OK) { turbo_sequence_deallocate(scratch); turbo_heap_discard_prepared(heap, prepared); return status; }
    status = turbo_sequence_move_destroy(heap->element_type, heap->elem_size,
        turbo_heap_slot(heap, heap->size), prepared);
    turbo_sequence_deallocate(prepared);
    if (status != CONTAINER_OK) { turbo_sequence_deallocate(scratch); return status; }
    ++heap->size;
    turbo_heap_sift_up(heap, heap->size - 1u, scratch);
    turbo_sequence_deallocate(scratch);
    ++heap->generation;
    return CONTAINER_OK;
}
container_status turbo_heap_pop(turbo_heap_t *heap, void *out_elem) {
    void *scratch = NULL;
    container_status status;
    size_t last;
    if (!turbo_heap_valid(heap)) return CONTAINER_INVALID_ARGUMENT;
    if (!heap->size) return CONTAINER_EMPTY;
    if (heap->size > 1u) {
        status = turbo_sequence_allocate(1u, heap->elem_stride, heap->elem_align, &scratch);
        if (status != CONTAINER_OK) return status;
    }
    status = out_elem ? turbo_sequence_move_destroy(heap->element_type, heap->elem_size, out_elem,
                                                     turbo_heap_slot(heap, 0u))
                     : turbo_sequence_destroy_value(heap->element_type, turbo_heap_slot(heap, 0u));
    if (status != CONTAINER_OK) { turbo_sequence_deallocate(scratch); return status; }
    last = heap->size - 1u;
    if (last) {
        (void)turbo_sequence_move_destroy(heap->element_type, heap->elem_size,
            turbo_heap_slot(heap, 0u), turbo_heap_slot(heap, last));
    }
    heap->size = last;
    if (heap->size) turbo_heap_sift_down(heap, 0u, scratch);
    turbo_sequence_deallocate(scratch);
    ++heap->generation;
    return CONTAINER_OK;
}

const void *turbo_heap_peek(const turbo_heap_t *heap) {
    return turbo_heap_valid(heap) && heap->size ? heap->data : NULL;
}
size_t turbo_heap_size(const turbo_heap_t *heap) { return turbo_heap_valid(heap) ? heap->size : 0u; }
size_t turbo_heap_capacity(const turbo_heap_t *heap) { return turbo_heap_valid(heap) ? heap->capacity : 0u; }
uint64_t turbo_heap_generation(const turbo_heap_t *heap) { return heap ? heap->generation : UINT64_C(0); }
bool turbo_heap_empty(const turbo_heap_t *heap) { return turbo_heap_size(heap) == 0u; }
