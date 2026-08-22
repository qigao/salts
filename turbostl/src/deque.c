#include <turbostl/deque.h>

#include "sequence_internal.h"

#include <string.h>

#define TURBO_DEQUE_MIN_CAPACITY 8u

static bool deque_valid(const deque_t *deque) {
    return deque != NULL && deque->initialized && deque->elem_size != 0u;
}
static size_t deque_physical(const deque_t *deque, size_t index) {
    return (deque->head + index) % deque->capacity;
}
static unsigned char *deque_slot(deque_t *deque, size_t physical) {
    return (unsigned char *)deque->data + physical * deque->elem_stride;
}
static const unsigned char *deque_slot_const(const deque_t *deque, size_t physical) {
    return (const unsigned char *)deque->data + physical * deque->elem_stride;
}
static unsigned char *deque_at_slot(deque_t *deque, size_t index) {
    return deque_slot(deque, deque_physical(deque, index));
}
static const unsigned char *deque_at_slot_const(const deque_t *deque, size_t index) {
    return deque_slot_const(deque, deque_physical(deque, index));
}

static turbostl_status deque_next_capacity(const deque_t *deque, size_t minimum,
                                                  size_t *out_capacity) {
    size_t capacity;
    if (minimum > deque->element_limit) return TURBO_STL_CAPACITY_EXCEEDED;
    capacity = deque->capacity ? deque->capacity : TURBO_DEQUE_MIN_CAPACITY;
    if (capacity > deque->element_limit) capacity = deque->element_limit;
    while (capacity < minimum) {
        if (capacity > deque->element_limit - capacity) { capacity = deque->element_limit; break; }
        capacity *= 2u;
    }
    *out_capacity = capacity;
    return TURBO_STL_OK;
}

static turbostl_status deque_grow_to(deque_t *deque, size_t minimum, bool *changed) {
    void *data;
    size_t capacity, index;
    turbostl_status status;
    if (changed) *changed = false;
    if (!deque_valid(deque)) return TURBO_STL_INVALID_ARGUMENT;
    if (minimum <= deque->capacity) return TURBO_STL_OK;
    status = deque_next_capacity(deque, minimum, &capacity);
    if (status != TURBO_STL_OK) return status;
    status = sequence_allocate(capacity, deque->elem_stride, deque->elem_align, &data);
    if (status != TURBO_STL_OK) return status;
    if (!deque->element_type) {
        for (index = 0u; index < deque->size; ++index)
            memcpy((unsigned char *)data + index * deque->elem_stride,
                   deque_at_slot_const(deque, index), deque->elem_size);
    } else {
        for (index = 0u; index < deque->size; ++index) {
            status = sequence_move_destroy(deque->element_type, deque->elem_size,
                (unsigned char *)data + index * deque->elem_stride, deque_at_slot(deque, index));
            if (status != TURBO_STL_OK) { sequence_deallocate(data); return status; }
        }
    }
    sequence_deallocate(deque->data);
    deque->data = data; deque->capacity = capacity; deque->head = 0u;
    if (changed) *changed = true;
    return TURBO_STL_OK;
}

static turbostl_status deque_prepare_copy(const deque_t *deque, const void *elem,
                                                  void **out_value) {
    turbostl_status status;
    if (!elem || !out_value) return TURBO_STL_INVALID_ARGUMENT;
    status = sequence_allocate(1u, deque->elem_stride, deque->elem_align, out_value);
    if (status != TURBO_STL_OK) return status;
    status = sequence_copy(deque->element_type, deque->elem_size, *out_value, elem);
    if (status != TURBO_STL_OK) { sequence_deallocate(*out_value); *out_value = NULL; }
    return status;
}
static void deque_discard_prepared(const deque_t *deque, void *value) {
    if (value) { (void)sequence_destroy_value(deque->element_type, value); sequence_deallocate(value); }
}

static turbostl_status deque_initialize(deque_t *deque, const cmeta_type_desc *type,
                                               size_t size, size_t align, size_t limit) {
    turbostl_status status;
    size_t stride;
    uint64_t generation;
    if (!deque) return TURBO_STL_INVALID_ARGUMENT;
    if (deque->initialized) return TURBO_STL_INVALID_ARGUMENT;
    status = sequence_stride(size, align, &stride);
    if (status != TURBO_STL_OK) return status;
    generation = deque->generation + UINT64_C(1);
    memset(deque, 0, sizeof(*deque));
    deque->elem_size = size; deque->elem_stride = stride; deque->elem_align = align;
    deque->element_limit = limit; deque->element_type = type; deque->generation = generation;
    deque->initialized = true;
    return TURBO_STL_OK;
}

turbostl_status deque_init_bytes(deque_t *deque, size_t elem_size, size_t elem_align,
                                        size_t element_limit) {
    return deque_initialize(deque, NULL, elem_size, elem_align, element_limit);
}
turbostl_status deque_init(deque_t *deque, const cmeta_type_desc *element_type,
                                  size_t element_limit) {
    turbostl_status status;
    if (!deque || deque->initialized) return TURBO_STL_INVALID_ARGUMENT;
    status = sequence_require_type(element_type, false);
    if (status != TURBO_STL_OK) return status;
    return deque_initialize(deque, element_type, element_type->size, element_type->align,
                                  element_limit);
}

turbostl_status deque_from_array_bytes(deque_t *deque, const void *elements,
                                              size_t count, size_t elem_size, size_t elem_align,
                                              size_t limit) {
    deque_t temporary = {0};
    turbostl_status status;
    size_t index;
    uint64_t generation;
    if (!deque) return TURBO_STL_INVALID_ARGUMENT;
    if (deque->initialized) return TURBO_STL_INVALID_ARGUMENT;
    if ((count && !elements) || count > limit)
        return count > limit ? TURBO_STL_CAPACITY_EXCEEDED : TURBO_STL_INVALID_ARGUMENT;
    status = deque_init_bytes(&temporary, elem_size, elem_align, limit);
    if (status != TURBO_STL_OK) return status;
    for (index = 0u; index < count; ++index) {
        status = deque_push_back(&temporary, (const unsigned char *)elements + index * elem_size);
        if (status != TURBO_STL_OK) { deque_destroy(&temporary); return status; }
    }
    generation = deque->generation + UINT64_C(1);
    temporary.generation = generation;
    *deque = temporary;
    return TURBO_STL_OK;
}
turbostl_status deque_from_array(deque_t *deque, const void *elements, size_t count,
                                        const cmeta_type_desc *type, size_t limit) {
    deque_t temporary = {0};
    turbostl_status status;
    size_t index;
    uint64_t generation;
    if (!deque) return TURBO_STL_INVALID_ARGUMENT;
    if (deque->initialized) return TURBO_STL_INVALID_ARGUMENT;
    if ((count && !elements) || count > limit)
        return count > limit ? TURBO_STL_CAPACITY_EXCEEDED : TURBO_STL_INVALID_ARGUMENT;
    status = deque_init(&temporary, type, limit);
    if (status != TURBO_STL_OK) return status;
    for (index = 0u; index < count; ++index) {
        status = deque_push_back(&temporary, (const unsigned char *)elements + index * type->size);
        if (status != TURBO_STL_OK) { deque_destroy(&temporary); return status; }
    }
    generation = deque->generation + UINT64_C(1);
    temporary.generation = generation;
    *deque = temporary;
    return TURBO_STL_OK;
}

void deque_destroy(deque_t *deque) {
    uint64_t generation;
    if (!deque) return;
    generation = deque->generation;
    if (deque->initialized) {
        (void)deque_clear(deque);
        generation = deque->generation + UINT64_C(1);
        sequence_deallocate(deque->data);
    }
    memset(deque, 0, sizeof(*deque));
    deque->generation = generation;
}
turbostl_status deque_clear(deque_t *deque) {
    size_t index;
    turbostl_status status;
    if (!deque_valid(deque)) return TURBO_STL_INVALID_ARGUMENT;
    for (index = 0u; index < deque->size; ++index) {
        status = sequence_destroy_value(deque->element_type, deque_at_slot(deque, index));
        if (status != TURBO_STL_OK) return status;
    }
    if (deque->size) { deque->size = 0u; deque->head = 0u; ++deque->generation; }
    return TURBO_STL_OK;
}
turbostl_status deque_reserve(deque_t *deque, size_t min_capacity) {
    bool changed;
    turbostl_status status = deque_grow_to(deque, min_capacity, &changed);
    if (status == TURBO_STL_OK && changed) ++deque->generation;
    return status;
}

turbostl_status deque_push_back(deque_t *deque, const void *elem) {
    void *prepared = NULL;
    turbostl_status status;
    size_t physical;
    if (!deque_valid(deque) || !elem) return TURBO_STL_INVALID_ARGUMENT;
    if (deque->size >= deque->element_limit) return TURBO_STL_CAPACITY_EXCEEDED;
    status = deque_prepare_copy(deque, elem, &prepared);
    if (status != TURBO_STL_OK) return status;
    status = deque_grow_to(deque, deque->size + 1u, NULL);
    if (status != TURBO_STL_OK) { deque_discard_prepared(deque, prepared); return status; }
    physical = deque_physical(deque, deque->size);
    status = sequence_move_destroy(deque->element_type, deque->elem_size,
        deque_slot(deque, physical), prepared);
    sequence_deallocate(prepared);
    if (status != TURBO_STL_OK) return status;
    ++deque->size; ++deque->generation;
    return TURBO_STL_OK;
}
turbostl_status deque_push_front(deque_t *deque, const void *elem) {
    void *prepared = NULL;
    turbostl_status status;
    size_t head;
    if (!deque_valid(deque) || !elem) return TURBO_STL_INVALID_ARGUMENT;
    if (deque->size >= deque->element_limit) return TURBO_STL_CAPACITY_EXCEEDED;
    status = deque_prepare_copy(deque, elem, &prepared);
    if (status != TURBO_STL_OK) return status;
    status = deque_grow_to(deque, deque->size + 1u, NULL);
    if (status != TURBO_STL_OK) { deque_discard_prepared(deque, prepared); return status; }
    head = deque->head == 0u ? deque->capacity - 1u : deque->head - 1u;
    status = sequence_move_destroy(deque->element_type, deque->elem_size,
        deque_slot(deque, head), prepared);
    sequence_deallocate(prepared);
    if (status != TURBO_STL_OK) return status;
    deque->head = head; ++deque->size; ++deque->generation;
    return TURBO_STL_OK;
}
turbostl_status deque_pop_back(deque_t *deque, void *out_elem) {
    turbostl_status status;
    size_t physical;
    if (!deque_valid(deque)) return TURBO_STL_INVALID_ARGUMENT;
    if (!deque->size) return TURBO_STL_EMPTY;
    physical = deque_physical(deque, deque->size - 1u);
    status = out_elem ? sequence_move_destroy(deque->element_type, deque->elem_size, out_elem,
                                                      deque_slot(deque, physical))
                     : sequence_destroy_value(deque->element_type, deque_slot(deque, physical));
    if (status != TURBO_STL_OK) return status;
    --deque->size; if (!deque->size) deque->head = 0u; ++deque->generation;
    return TURBO_STL_OK;
}
turbostl_status deque_pop_front(deque_t *deque, void *out_elem) {
    turbostl_status status;
    if (!deque_valid(deque)) return TURBO_STL_INVALID_ARGUMENT;
    if (!deque->size) return TURBO_STL_EMPTY;
    status = out_elem ? sequence_move_destroy(deque->element_type, deque->elem_size, out_elem,
                                                      deque_slot(deque, deque->head))
                     : sequence_destroy_value(deque->element_type, deque_slot(deque, deque->head));
    if (status != TURBO_STL_OK) return status;
    deque->head = (deque->head + 1u) % deque->capacity;
    --deque->size; if (!deque->size) deque->head = 0u; ++deque->generation;
    return TURBO_STL_OK;
}
turbostl_status deque_set(deque_t *deque, size_t index, const void *elem) {
    void *prepared = NULL;
    turbostl_status status;
    unsigned char *slot;
    if (!deque_valid(deque) || !elem || index >= deque->size) return TURBO_STL_INVALID_ARGUMENT;
    status = deque_prepare_copy(deque, elem, &prepared);
    if (status != TURBO_STL_OK) return status;
    slot = deque_at_slot(deque, index);
    status = sequence_destroy_value(deque->element_type, slot);
    if (status == TURBO_STL_OK)
        status = sequence_move_destroy(deque->element_type, deque->elem_size, slot, prepared);
    sequence_deallocate(prepared);
    if (status != TURBO_STL_OK) return status;
    ++deque->generation;
    return TURBO_STL_OK;
}

void *deque_front(deque_t *deque) {
    return deque_valid(deque) && deque->size ? deque_slot(deque, deque->head) : NULL;
}
const void *deque_front_const(const deque_t *deque) {
    return deque_valid(deque) && deque->size ? deque_slot_const(deque, deque->head) : NULL;
}
void *deque_back(deque_t *deque) {
    return deque_valid(deque) && deque->size ? deque_at_slot(deque, deque->size - 1u) : NULL;
}
const void *deque_back_const(const deque_t *deque) {
    return deque_valid(deque) && deque->size ? deque_at_slot_const(deque, deque->size - 1u) : NULL;
}
void *deque_at(deque_t *deque, size_t index) {
    return deque_valid(deque) && index < deque->size ? deque_at_slot(deque, index) : NULL;
}
const void *deque_at_const(const deque_t *deque, size_t index) {
    return deque_valid(deque) && index < deque->size ? deque_at_slot_const(deque, index) : NULL;
}
size_t deque_size(const deque_t *deque) { return deque_valid(deque) ? deque->size : 0u; }
size_t deque_capacity(const deque_t *deque) { return deque_valid(deque) ? deque->capacity : 0u; }
uint64_t deque_generation(const deque_t *deque) { return deque ? deque->generation : UINT64_C(0); }
bool deque_empty(const deque_t *deque) { return deque_size(deque) == 0u; }
