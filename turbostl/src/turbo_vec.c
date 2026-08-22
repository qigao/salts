#include <turbostl/vec.h>

#include "turbo_sequence_internal.h"

#include <string.h>

#define TURBO_VEC_MIN_CAPACITY 8u

static bool turbo_vec_valid(const turbo_vec_t *vec) {
    return vec != NULL && vec->initialized && vec->elem_size != 0u;
}

static unsigned char *turbo_vec_slot(turbo_vec_t *vec, size_t index) {
    return (unsigned char *)vec->data + index * vec->elem_stride;
}

static const unsigned char *turbo_vec_slot_const(const turbo_vec_t *vec, size_t index) {
    return (const unsigned char *)vec->data + index * vec->elem_stride;
}

static turbo_stl_status turbo_vec_next_capacity(const turbo_vec_t *vec, size_t minimum,
                                                size_t *out_capacity) {
    size_t capacity;
    if (minimum > vec->element_limit) return TURBO_STL_CAPACITY_EXCEEDED;
    capacity = vec->capacity ? vec->capacity : TURBO_VEC_MIN_CAPACITY;
    if (capacity > vec->element_limit) capacity = vec->element_limit;
    while (capacity < minimum) {
        if (capacity > vec->element_limit - capacity) { capacity = vec->element_limit; break; }
        capacity *= 2u;
    }
    *out_capacity = capacity;
    return TURBO_STL_OK;
}

static turbo_stl_status turbo_vec_grow_to(turbo_vec_t *vec, size_t minimum, bool *changed) {
    void *data;
    size_t capacity, index;
    turbo_stl_status status;
    if (changed) *changed = false;
    if (!turbo_vec_valid(vec)) return TURBO_STL_INVALID_ARGUMENT;
    if (minimum <= vec->capacity) return TURBO_STL_OK;
    status = turbo_vec_next_capacity(vec, minimum, &capacity);
    if (status != TURBO_STL_OK) return status;
    status = turbo_sequence_allocate(capacity, vec->elem_stride, vec->elem_align, &data);
    if (status != TURBO_STL_OK) return status;
    if (!vec->element_type) {
        if (vec->size) memcpy(data, vec->data, vec->size * vec->elem_stride);
    } else {
        for (index = 0u; index < vec->size; ++index) {
            status = turbo_sequence_move_destroy(vec->element_type, vec->elem_size,
                (unsigned char *)data + index * vec->elem_stride, turbo_vec_slot(vec, index));
            if (status != TURBO_STL_OK) { turbo_sequence_deallocate(data); return status; }
        }
    }
    turbo_sequence_deallocate(vec->data);
    vec->data = data;
    vec->capacity = capacity;
    if (changed) *changed = true;
    return TURBO_STL_OK;
}

static turbo_stl_status turbo_vec_prepare_copy(const turbo_vec_t *vec, const void *elem,
                                                void **out_value) {
    turbo_stl_status status;
    if (!elem || !out_value) return TURBO_STL_INVALID_ARGUMENT;
    status = turbo_sequence_allocate(1u, vec->elem_stride, vec->elem_align, out_value);
    if (status != TURBO_STL_OK) return status;
    status = turbo_sequence_copy(vec->element_type, vec->elem_size, *out_value, elem);
    if (status != TURBO_STL_OK) { turbo_sequence_deallocate(*out_value); *out_value = NULL; }
    return status;
}

static void turbo_vec_discard_prepared(const turbo_vec_t *vec, void *value) {
    if (!value) return;
    (void)turbo_sequence_destroy_value(vec->element_type, value);
    turbo_sequence_deallocate(value);
}

static turbo_stl_status turbo_vec_initialize(turbo_vec_t *vec, const cmeta_type_desc *type,
                                             size_t elem_size, size_t elem_align, size_t limit) {
    turbo_stl_status status;
    size_t stride;
    uint64_t generation;
    if (!vec) return TURBO_STL_INVALID_ARGUMENT;
    if (vec->initialized) return TURBO_STL_INVALID_ARGUMENT;
    status = turbo_sequence_stride(elem_size, elem_align, &stride);
    if (status != TURBO_STL_OK) return status;
    generation = vec->generation + UINT64_C(1);
    memset(vec, 0, sizeof(*vec));
    vec->elem_size = elem_size; vec->elem_stride = stride; vec->elem_align = elem_align;
    vec->element_limit = limit; vec->element_type = type; vec->generation = generation;
    vec->initialized = true;
    return TURBO_STL_OK;
}

turbo_stl_status turbo_vec_init_bytes(turbo_vec_t *vec, size_t elem_size, size_t elem_align,
                                      size_t element_limit) {
    return turbo_vec_initialize(vec, NULL, elem_size, elem_align, element_limit);
}

turbo_stl_status turbo_vec_init(turbo_vec_t *vec, const cmeta_type_desc *element_type,
                                size_t element_limit) {
    turbo_stl_status status;
    if (!vec || vec->initialized) return TURBO_STL_INVALID_ARGUMENT;
    status = turbo_sequence_require_type(element_type, false);
    if (status != TURBO_STL_OK) return status;
    return turbo_vec_initialize(vec, element_type, element_type->size, element_type->align,
                                element_limit);
}

turbo_stl_status turbo_vec_from_array_bytes(turbo_vec_t *vec, const void *elements, size_t count,
                                            size_t elem_size, size_t elem_align, size_t limit) {
    turbo_vec_t temporary = {0};
    turbo_stl_status status;
    size_t index;
    uint64_t generation;
    if (!vec) return TURBO_STL_INVALID_ARGUMENT;
    if (vec->initialized) return TURBO_STL_INVALID_ARGUMENT;
    if ((count && !elements) || count > limit)
        return count > limit ? TURBO_STL_CAPACITY_EXCEEDED : TURBO_STL_INVALID_ARGUMENT;
    status = turbo_vec_init_bytes(&temporary, elem_size, elem_align, limit);
    if (status != TURBO_STL_OK) return status;
    for (index = 0u; index < count; ++index) {
        status = turbo_vec_push(&temporary, (const unsigned char *)elements + index * elem_size);
        if (status != TURBO_STL_OK) { turbo_vec_destroy(&temporary); return status; }
    }
    generation = vec->generation + UINT64_C(1);
    temporary.generation = generation;
    *vec = temporary;
    return TURBO_STL_OK;
}

turbo_stl_status turbo_vec_from_array(turbo_vec_t *vec, const void *elements, size_t count,
                                      const cmeta_type_desc *type, size_t limit) {
    turbo_vec_t temporary = {0};
    turbo_stl_status status;
    size_t index;
    uint64_t generation;
    if (!vec) return TURBO_STL_INVALID_ARGUMENT;
    if (vec->initialized) return TURBO_STL_INVALID_ARGUMENT;
    if ((count && !elements) || count > limit)
        return count > limit ? TURBO_STL_CAPACITY_EXCEEDED : TURBO_STL_INVALID_ARGUMENT;
    status = turbo_vec_init(&temporary, type, limit);
    if (status != TURBO_STL_OK) return status;
    for (index = 0u; index < count; ++index) {
        status = turbo_vec_push(&temporary, (const unsigned char *)elements + index * type->size);
        if (status != TURBO_STL_OK) { turbo_vec_destroy(&temporary); return status; }
    }
    generation = vec->generation + UINT64_C(1);
    temporary.generation = generation;
    *vec = temporary;
    return TURBO_STL_OK;
}

void turbo_vec_destroy(turbo_vec_t *vec) {
    uint64_t generation;
    if (!vec) return;
    generation = vec->generation;
    if (vec->initialized) {
        (void)turbo_vec_clear(vec);
        generation = vec->generation + UINT64_C(1);
        turbo_sequence_deallocate(vec->data);
    }
    memset(vec, 0, sizeof(*vec));
    vec->generation = generation;
}

turbo_stl_status turbo_vec_clear(turbo_vec_t *vec) {
    size_t index;
    turbo_stl_status status;
    if (!turbo_vec_valid(vec)) return TURBO_STL_INVALID_ARGUMENT;
    for (index = 0u; index < vec->size; ++index) {
        status = turbo_sequence_destroy_value(vec->element_type, turbo_vec_slot(vec, index));
        if (status != TURBO_STL_OK) return status;
    }
    if (vec->size) { vec->size = 0u; ++vec->generation; }
    return TURBO_STL_OK;
}

turbo_stl_status turbo_vec_reserve(turbo_vec_t *vec, size_t min_capacity) {
    bool changed;
    turbo_stl_status status = turbo_vec_grow_to(vec, min_capacity, &changed);
    if (status == TURBO_STL_OK && changed) ++vec->generation;
    return status;
}

turbo_stl_status turbo_vec_resize(turbo_vec_t *vec, size_t new_size) {
    size_t old_size;
    turbo_stl_status status;
    if (!turbo_vec_valid(vec)) return TURBO_STL_INVALID_ARGUMENT;
    if (new_size > vec->element_limit) return TURBO_STL_CAPACITY_EXCEEDED;
    old_size = vec->size;
    if (new_size == old_size) return TURBO_STL_OK;
    if (new_size < old_size) {
        while (vec->size > new_size) {
            --vec->size;
            status = turbo_sequence_destroy_value(vec->element_type, turbo_vec_slot(vec, vec->size));
            if (status != TURBO_STL_OK) return status;
        }
        ++vec->generation;
        return TURBO_STL_OK;
    }
    if (vec->element_type) return TURBO_STL_TRAIT_MISSING;
    status = turbo_vec_grow_to(vec, new_size, NULL);
    if (status != TURBO_STL_OK) return status;
    memset(turbo_vec_slot(vec, old_size), 0, (new_size - old_size) * vec->elem_stride);
    vec->size = new_size;
    ++vec->generation;
    return TURBO_STL_OK;
}

turbo_stl_status turbo_vec_push(turbo_vec_t *vec, const void *elem) {
    void *prepared = NULL;
    turbo_stl_status status;
    if (!turbo_vec_valid(vec) || !elem) return TURBO_STL_INVALID_ARGUMENT;
    if (vec->size >= vec->element_limit) return TURBO_STL_CAPACITY_EXCEEDED;
    status = turbo_vec_prepare_copy(vec, elem, &prepared);
    if (status != TURBO_STL_OK) return status;
    status = turbo_vec_grow_to(vec, vec->size + 1u, NULL);
    if (status != TURBO_STL_OK) { turbo_vec_discard_prepared(vec, prepared); return status; }
    status = turbo_sequence_move_destroy(vec->element_type, vec->elem_size,
        turbo_vec_slot(vec, vec->size), prepared);
    turbo_sequence_deallocate(prepared);
    if (status != TURBO_STL_OK) return status;
    ++vec->size; ++vec->generation;
    return TURBO_STL_OK;
}

turbo_stl_status turbo_vec_pop(turbo_vec_t *vec, void *out_elem) {
    turbo_stl_status status;
    size_t index;
    if (!turbo_vec_valid(vec)) return TURBO_STL_INVALID_ARGUMENT;
    if (!vec->size) return TURBO_STL_EMPTY;
    index = vec->size - 1u;
    status = out_elem ? turbo_sequence_move_destroy(vec->element_type, vec->elem_size, out_elem,
                                                     turbo_vec_slot(vec, index))
                     : turbo_sequence_destroy_value(vec->element_type, turbo_vec_slot(vec, index));
    if (status != TURBO_STL_OK) return status;
    vec->size = index; ++vec->generation;
    return TURBO_STL_OK;
}

turbo_stl_status turbo_vec_insert(turbo_vec_t *vec, size_t index, const void *elem) {
    void *prepared = NULL;
    size_t cursor;
    turbo_stl_status status;
    if (!turbo_vec_valid(vec) || !elem || index > vec->size) return TURBO_STL_INVALID_ARGUMENT;
    if (vec->size >= vec->element_limit) return TURBO_STL_CAPACITY_EXCEEDED;
    status = turbo_vec_prepare_copy(vec, elem, &prepared);
    if (status != TURBO_STL_OK) return status;
    status = turbo_vec_grow_to(vec, vec->size + 1u, NULL);
    if (status != TURBO_STL_OK) { turbo_vec_discard_prepared(vec, prepared); return status; }
    if (!vec->element_type) {
        memmove(turbo_vec_slot(vec, index + 1u), turbo_vec_slot(vec, index),
                (vec->size - index) * vec->elem_stride);
    } else {
        for (cursor = vec->size; cursor > index; --cursor)
            (void)turbo_sequence_move_destroy(vec->element_type, vec->elem_size,
                turbo_vec_slot(vec, cursor), turbo_vec_slot(vec, cursor - 1u));
    }
    status = turbo_sequence_move_destroy(vec->element_type, vec->elem_size,
        turbo_vec_slot(vec, index), prepared);
    turbo_sequence_deallocate(prepared);
    if (status != TURBO_STL_OK) return status;
    ++vec->size; ++vec->generation;
    return TURBO_STL_OK;
}

turbo_stl_status turbo_vec_set(turbo_vec_t *vec, size_t index, const void *elem) {
    void *prepared = NULL;
    turbo_stl_status status;
    if (!turbo_vec_valid(vec) || !elem || index >= vec->size) return TURBO_STL_INVALID_ARGUMENT;
    status = turbo_vec_prepare_copy(vec, elem, &prepared);
    if (status != TURBO_STL_OK) return status;
    status = turbo_sequence_destroy_value(vec->element_type, turbo_vec_slot(vec, index));
    if (status == TURBO_STL_OK)
        status = turbo_sequence_move_destroy(vec->element_type, vec->elem_size,
            turbo_vec_slot(vec, index), prepared);
    turbo_sequence_deallocate(prepared);
    if (status != TURBO_STL_OK) return status;
    ++vec->generation;
    return TURBO_STL_OK;
}

turbo_stl_status turbo_vec_erase(turbo_vec_t *vec, size_t index, void *out_elem) {
    size_t cursor;
    turbo_stl_status status;
    if (!turbo_vec_valid(vec) || index >= vec->size) return TURBO_STL_INVALID_ARGUMENT;
    status = out_elem ? turbo_sequence_move_destroy(vec->element_type, vec->elem_size, out_elem,
                                                     turbo_vec_slot(vec, index))
                     : turbo_sequence_destroy_value(vec->element_type, turbo_vec_slot(vec, index));
    if (status != TURBO_STL_OK) return status;
    if (!vec->element_type) {
        memmove(turbo_vec_slot(vec, index), turbo_vec_slot(vec, index + 1u),
                (vec->size - index - 1u) * vec->elem_stride);
    } else {
        for (cursor = index; cursor + 1u < vec->size; ++cursor)
            (void)turbo_sequence_move_destroy(vec->element_type, vec->elem_size,
                turbo_vec_slot(vec, cursor), turbo_vec_slot(vec, cursor + 1u));
    }
    --vec->size; ++vec->generation;
    return TURBO_STL_OK;
}

turbo_stl_status turbo_vec_swap_remove(turbo_vec_t *vec, size_t index, void *out_elem) {
    turbo_stl_status status;
    size_t last;
    if (!turbo_vec_valid(vec) || index >= vec->size) return TURBO_STL_INVALID_ARGUMENT;
    last = vec->size - 1u;
    status = out_elem ? turbo_sequence_move_destroy(vec->element_type, vec->elem_size, out_elem,
                                                     turbo_vec_slot(vec, index))
                     : turbo_sequence_destroy_value(vec->element_type, turbo_vec_slot(vec, index));
    if (status != TURBO_STL_OK) return status;
    if (index != last) {
        status = turbo_sequence_move_destroy(vec->element_type, vec->elem_size,
            turbo_vec_slot(vec, index), turbo_vec_slot(vec, last));
        if (status != TURBO_STL_OK) return status;
    }
    --vec->size; ++vec->generation;
    return TURBO_STL_OK;
}

void *turbo_vec_at(turbo_vec_t *vec, size_t index) {
    return turbo_vec_valid(vec) && index < vec->size ? turbo_vec_slot(vec, index) : NULL;
}
const void *turbo_vec_at_const(const turbo_vec_t *vec, size_t index) {
    return turbo_vec_valid(vec) && index < vec->size ? turbo_vec_slot_const(vec, index) : NULL;
}
void *turbo_vec_data(turbo_vec_t *vec) { return turbo_vec_valid(vec) ? vec->data : NULL; }
const void *turbo_vec_data_const(const turbo_vec_t *vec) { return turbo_vec_valid(vec) ? vec->data : NULL; }
size_t turbo_vec_size(const turbo_vec_t *vec) { return turbo_vec_valid(vec) ? vec->size : 0u; }
size_t turbo_vec_capacity(const turbo_vec_t *vec) { return turbo_vec_valid(vec) ? vec->capacity : 0u; }
uint64_t turbo_vec_generation(const turbo_vec_t *vec) { return vec ? vec->generation : UINT64_C(0); }
bool turbo_vec_empty(const turbo_vec_t *vec) { return turbo_vec_size(vec) == 0u; }
