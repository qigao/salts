#include <turbo/container/deque.h>

#include "turbo_sequence_internal.h"

#include <string.h>

#define TURBO_DEQUE_MIN_CAPACITY 8u

static bool turbo_deque_valid(const turbo_deque_t *deque) {
    return deque != NULL && deque->initialized && deque->elem_size != 0u;
}
static size_t turbo_deque_physical(const turbo_deque_t *deque, size_t index) {
    return (deque->head + index) % deque->capacity;
}
static unsigned char *turbo_deque_slot(turbo_deque_t *deque, size_t physical) {
    return (unsigned char *)deque->data + physical * deque->elem_stride;
}
static const unsigned char *turbo_deque_slot_const(const turbo_deque_t *deque, size_t physical) {
    return (const unsigned char *)deque->data + physical * deque->elem_stride;
}
static unsigned char *turbo_deque_at_slot(turbo_deque_t *deque, size_t index) {
    return turbo_deque_slot(deque, turbo_deque_physical(deque, index));
}
static const unsigned char *turbo_deque_at_slot_const(const turbo_deque_t *deque, size_t index) {
    return turbo_deque_slot_const(deque, turbo_deque_physical(deque, index));
}

static container_status turbo_deque_next_capacity(const turbo_deque_t *deque, size_t minimum,
                                                  size_t *out_capacity) {
    size_t capacity;
    if (minimum > deque->element_limit) return CONTAINER_CAPACITY_EXCEEDED;
    capacity = deque->capacity ? deque->capacity : TURBO_DEQUE_MIN_CAPACITY;
    if (capacity > deque->element_limit) capacity = deque->element_limit;
    while (capacity < minimum) {
        if (capacity > deque->element_limit - capacity) { capacity = deque->element_limit; break; }
        capacity *= 2u;
    }
    *out_capacity = capacity;
    return CONTAINER_OK;
}

static container_status turbo_deque_grow_to(turbo_deque_t *deque, size_t minimum, bool *changed) {
    void *data;
    size_t capacity, index;
    container_status status;
    if (changed) *changed = false;
    if (!turbo_deque_valid(deque)) return CONTAINER_INVALID_ARGUMENT;
    if (minimum <= deque->capacity) return CONTAINER_OK;
    status = turbo_deque_next_capacity(deque, minimum, &capacity);
    if (status != CONTAINER_OK) return status;
    status = turbo_sequence_allocate(capacity, deque->elem_stride, deque->elem_align, &data);
    if (status != CONTAINER_OK) return status;
    if (!deque->element_type) {
        for (index = 0u; index < deque->size; ++index)
            memcpy((unsigned char *)data + index * deque->elem_stride,
                   turbo_deque_at_slot_const(deque, index), deque->elem_size);
    } else {
        for (index = 0u; index < deque->size; ++index) {
            status = turbo_sequence_move_destroy(deque->element_type, deque->elem_size,
                (unsigned char *)data + index * deque->elem_stride, turbo_deque_at_slot(deque, index));
            if (status != CONTAINER_OK) { turbo_sequence_deallocate(data); return status; }
        }
    }
    turbo_sequence_deallocate(deque->data);
    deque->data = data; deque->capacity = capacity; deque->head = 0u;
    if (changed) *changed = true;
    return CONTAINER_OK;
}

static container_status turbo_deque_prepare_copy(const turbo_deque_t *deque, const void *elem,
                                                  void **out_value) {
    container_status status;
    if (!elem || !out_value) return CONTAINER_INVALID_ARGUMENT;
    status = turbo_sequence_allocate(1u, deque->elem_stride, deque->elem_align, out_value);
    if (status != CONTAINER_OK) return status;
    status = turbo_sequence_copy(deque->element_type, deque->elem_size, *out_value, elem);
    if (status != CONTAINER_OK) { turbo_sequence_deallocate(*out_value); *out_value = NULL; }
    return status;
}
static void turbo_deque_discard_prepared(const turbo_deque_t *deque, void *value) {
    if (value) { (void)turbo_sequence_destroy_value(deque->element_type, value); turbo_sequence_deallocate(value); }
}

static container_status turbo_deque_initialize(turbo_deque_t *deque, const cmeta_type_desc *type,
                                               size_t size, size_t align, size_t limit) {
    container_status status;
    size_t stride;
    uint64_t generation;
    if (!deque) return CONTAINER_INVALID_ARGUMENT;
    if (deque->initialized) return CONTAINER_INVALID_ARGUMENT;
    status = turbo_sequence_stride(size, align, &stride);
    if (status != CONTAINER_OK) return status;
    generation = deque->generation + UINT64_C(1);
    memset(deque, 0, sizeof(*deque));
    deque->elem_size = size; deque->elem_stride = stride; deque->elem_align = align;
    deque->element_limit = limit; deque->element_type = type; deque->generation = generation;
    deque->initialized = true;
    return CONTAINER_OK;
}

container_status turbo_deque_init_bytes(turbo_deque_t *deque, size_t elem_size, size_t elem_align,
                                        size_t element_limit) {
    return turbo_deque_initialize(deque, NULL, elem_size, elem_align, element_limit);
}
container_status turbo_deque_init(turbo_deque_t *deque, const cmeta_type_desc *element_type,
                                  size_t element_limit) {
    container_status status = turbo_sequence_require_type(element_type, false);
    if (status != CONTAINER_OK) return status;
    return turbo_deque_initialize(deque, element_type, element_type->size, element_type->align,
                                  element_limit);
}

container_status turbo_deque_from_array_bytes(turbo_deque_t *deque, const void *elements,
                                              size_t count, size_t elem_size, size_t elem_align,
                                              size_t limit) {
    container_status status;
    size_t index;
    if ((count && !elements) || count > limit)
        return count > limit ? CONTAINER_CAPACITY_EXCEEDED : CONTAINER_INVALID_ARGUMENT;
    status = turbo_deque_init_bytes(deque, elem_size, elem_align, limit);
    if (status != CONTAINER_OK) return status;
    for (index = 0u; index < count; ++index) {
        status = turbo_deque_push_back(deque, (const unsigned char *)elements + index * elem_size);
        if (status != CONTAINER_OK) { turbo_deque_destroy(deque); return status; }
    }
    return CONTAINER_OK;
}
container_status turbo_deque_from_array(turbo_deque_t *deque, const void *elements, size_t count,
                                        const cmeta_type_desc *type, size_t limit) {
    container_status status;
    size_t index;
    if ((count && !elements) || count > limit)
        return count > limit ? CONTAINER_CAPACITY_EXCEEDED : CONTAINER_INVALID_ARGUMENT;
    status = turbo_deque_init(deque, type, limit);
    if (status != CONTAINER_OK) return status;
    for (index = 0u; index < count; ++index) {
        status = turbo_deque_push_back(deque, (const unsigned char *)elements + index * type->size);
        if (status != CONTAINER_OK) { turbo_deque_destroy(deque); return status; }
    }
    return CONTAINER_OK;
}

void turbo_deque_destroy(turbo_deque_t *deque) {
    uint64_t generation;
    if (!deque) return;
    generation = deque->generation;
    if (deque->initialized) {
        (void)turbo_deque_clear(deque);
        generation = deque->generation + UINT64_C(1);
        turbo_sequence_deallocate(deque->data);
    }
    memset(deque, 0, sizeof(*deque));
    deque->generation = generation;
}
container_status turbo_deque_clear(turbo_deque_t *deque) {
    size_t index;
    container_status status;
    if (!turbo_deque_valid(deque)) return CONTAINER_INVALID_ARGUMENT;
    for (index = 0u; index < deque->size; ++index) {
        status = turbo_sequence_destroy_value(deque->element_type, turbo_deque_at_slot(deque, index));
        if (status != CONTAINER_OK) return status;
    }
    if (deque->size) { deque->size = 0u; deque->head = 0u; ++deque->generation; }
    return CONTAINER_OK;
}
container_status turbo_deque_reserve(turbo_deque_t *deque, size_t min_capacity) {
    bool changed;
    container_status status = turbo_deque_grow_to(deque, min_capacity, &changed);
    if (status == CONTAINER_OK && changed) ++deque->generation;
    return status;
}

container_status turbo_deque_push_back(turbo_deque_t *deque, const void *elem) {
    void *prepared = NULL;
    container_status status;
    size_t physical;
    if (!turbo_deque_valid(deque) || !elem) return CONTAINER_INVALID_ARGUMENT;
    if (deque->size >= deque->element_limit) return CONTAINER_CAPACITY_EXCEEDED;
    status = turbo_deque_prepare_copy(deque, elem, &prepared);
    if (status != CONTAINER_OK) return status;
    status = turbo_deque_grow_to(deque, deque->size + 1u, NULL);
    if (status != CONTAINER_OK) { turbo_deque_discard_prepared(deque, prepared); return status; }
    physical = turbo_deque_physical(deque, deque->size);
    status = turbo_sequence_move_destroy(deque->element_type, deque->elem_size,
        turbo_deque_slot(deque, physical), prepared);
    turbo_sequence_deallocate(prepared);
    if (status != CONTAINER_OK) return status;
    ++deque->size; ++deque->generation;
    return CONTAINER_OK;
}
container_status turbo_deque_push_front(turbo_deque_t *deque, const void *elem) {
    void *prepared = NULL;
    container_status status;
    size_t head;
    if (!turbo_deque_valid(deque) || !elem) return CONTAINER_INVALID_ARGUMENT;
    if (deque->size >= deque->element_limit) return CONTAINER_CAPACITY_EXCEEDED;
    status = turbo_deque_prepare_copy(deque, elem, &prepared);
    if (status != CONTAINER_OK) return status;
    status = turbo_deque_grow_to(deque, deque->size + 1u, NULL);
    if (status != CONTAINER_OK) { turbo_deque_discard_prepared(deque, prepared); return status; }
    head = deque->head == 0u ? deque->capacity - 1u : deque->head - 1u;
    status = turbo_sequence_move_destroy(deque->element_type, deque->elem_size,
        turbo_deque_slot(deque, head), prepared);
    turbo_sequence_deallocate(prepared);
    if (status != CONTAINER_OK) return status;
    deque->head = head; ++deque->size; ++deque->generation;
    return CONTAINER_OK;
}
container_status turbo_deque_pop_back(turbo_deque_t *deque, void *out_elem) {
    container_status status;
    size_t physical;
    if (!turbo_deque_valid(deque)) return CONTAINER_INVALID_ARGUMENT;
    if (!deque->size) return CONTAINER_EMPTY;
    physical = turbo_deque_physical(deque, deque->size - 1u);
    status = out_elem ? turbo_sequence_move_destroy(deque->element_type, deque->elem_size, out_elem,
                                                      turbo_deque_slot(deque, physical))
                     : turbo_sequence_destroy_value(deque->element_type, turbo_deque_slot(deque, physical));
    if (status != CONTAINER_OK) return status;
    --deque->size; if (!deque->size) deque->head = 0u; ++deque->generation;
    return CONTAINER_OK;
}
container_status turbo_deque_pop_front(turbo_deque_t *deque, void *out_elem) {
    container_status status;
    if (!turbo_deque_valid(deque)) return CONTAINER_INVALID_ARGUMENT;
    if (!deque->size) return CONTAINER_EMPTY;
    status = out_elem ? turbo_sequence_move_destroy(deque->element_type, deque->elem_size, out_elem,
                                                      turbo_deque_slot(deque, deque->head))
                     : turbo_sequence_destroy_value(deque->element_type, turbo_deque_slot(deque, deque->head));
    if (status != CONTAINER_OK) return status;
    deque->head = (deque->head + 1u) % deque->capacity;
    --deque->size; if (!deque->size) deque->head = 0u; ++deque->generation;
    return CONTAINER_OK;
}
container_status turbo_deque_set(turbo_deque_t *deque, size_t index, const void *elem) {
    void *prepared = NULL;
    container_status status;
    unsigned char *slot;
    if (!turbo_deque_valid(deque) || !elem || index >= deque->size) return CONTAINER_INVALID_ARGUMENT;
    status = turbo_deque_prepare_copy(deque, elem, &prepared);
    if (status != CONTAINER_OK) return status;
    slot = turbo_deque_at_slot(deque, index);
    status = turbo_sequence_destroy_value(deque->element_type, slot);
    if (status == CONTAINER_OK)
        status = turbo_sequence_move_destroy(deque->element_type, deque->elem_size, slot, prepared);
    turbo_sequence_deallocate(prepared);
    if (status != CONTAINER_OK) return status;
    ++deque->generation;
    return CONTAINER_OK;
}

void *turbo_deque_front(turbo_deque_t *deque) {
    return turbo_deque_valid(deque) && deque->size ? turbo_deque_slot(deque, deque->head) : NULL;
}
const void *turbo_deque_front_const(const turbo_deque_t *deque) {
    return turbo_deque_valid(deque) && deque->size ? turbo_deque_slot_const(deque, deque->head) : NULL;
}
void *turbo_deque_back(turbo_deque_t *deque) {
    return turbo_deque_valid(deque) && deque->size ? turbo_deque_at_slot(deque, deque->size - 1u) : NULL;
}
const void *turbo_deque_back_const(const turbo_deque_t *deque) {
    return turbo_deque_valid(deque) && deque->size ? turbo_deque_at_slot_const(deque, deque->size - 1u) : NULL;
}
void *turbo_deque_at(turbo_deque_t *deque, size_t index) {
    return turbo_deque_valid(deque) && index < deque->size ? turbo_deque_at_slot(deque, index) : NULL;
}
const void *turbo_deque_at_const(const turbo_deque_t *deque, size_t index) {
    return turbo_deque_valid(deque) && index < deque->size ? turbo_deque_at_slot_const(deque, index) : NULL;
}
size_t turbo_deque_size(const turbo_deque_t *deque) { return turbo_deque_valid(deque) ? deque->size : 0u; }
size_t turbo_deque_capacity(const turbo_deque_t *deque) { return turbo_deque_valid(deque) ? deque->capacity : 0u; }
uint64_t turbo_deque_generation(const turbo_deque_t *deque) { return deque ? deque->generation : UINT64_C(0); }
bool turbo_deque_empty(const turbo_deque_t *deque) { return turbo_deque_size(deque) == 0u; }
