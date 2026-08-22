#include <turbostl/vec.h>

#include "sequence_internal.h"

#include <string.h>

#define TURBO_VEC_MIN_CAPACITY 8u

static bool vec_valid(const vec_t *vec) {
    return vec != NULL && vec->initialized && vec->elem_size != 0u;
}

static unsigned char *vec_slot(vec_t *vec, size_t index) {
    return (unsigned char *)vec->data + index * vec->elem_stride;
}

static const unsigned char *vec_slot_const(const vec_t *vec, size_t index) {
    return (const unsigned char *)vec->data + index * vec->elem_stride;
}

static stl_status vec_next_capacity(const vec_t *vec, size_t minimum,
                                                size_t *out_capacity) {
    size_t capacity;
    if (minimum > vec->element_limit) return STL_CAPACITY_EXCEEDED;
    capacity = vec->capacity ? vec->capacity : TURBO_VEC_MIN_CAPACITY;
    if (capacity > vec->element_limit) capacity = vec->element_limit;
    while (capacity < minimum) {
        if (capacity > vec->element_limit - capacity) { capacity = vec->element_limit; break; }
        capacity *= 2u;
    }
    *out_capacity = capacity;
    return STL_OK;
}

static stl_status vec_grow_to(vec_t *vec, size_t minimum, bool *changed) {
    void *data;
    size_t capacity, index;
    stl_status status;
    if (changed) *changed = false;
    if (!vec_valid(vec)) return STL_INVALID_ARGUMENT;
    if (minimum <= vec->capacity) return STL_OK;
    status = vec_next_capacity(vec, minimum, &capacity);
    if (status != STL_OK) return status;
    status = sequence_allocate(capacity, vec->elem_stride, vec->elem_align, &data);
    if (status != STL_OK) return status;
    if (!vec->element_type) {
        if (vec->size) memcpy(data, vec->data, vec->size * vec->elem_stride);
    } else {
        for (index = 0u; index < vec->size; ++index) {
            status = sequence_move_destroy(vec->element_type, vec->elem_size,
                (unsigned char *)data + index * vec->elem_stride, vec_slot(vec, index));
            if (status != STL_OK) { sequence_deallocate(data); return status; }
        }
    }
    sequence_deallocate(vec->data);
    vec->data = data;
    vec->capacity = capacity;
    if (changed) *changed = true;
    return STL_OK;
}

static stl_status vec_prepare_copy(const vec_t *vec, const void *elem,
                                                void **out_value) {
    stl_status status;
    if (!elem || !out_value) return STL_INVALID_ARGUMENT;
    status = sequence_allocate(1u, vec->elem_stride, vec->elem_align, out_value);
    if (status != STL_OK) return status;
    status = sequence_copy(vec->element_type, vec->elem_size, *out_value, elem);
    if (status != STL_OK) { sequence_deallocate(*out_value); *out_value = NULL; }
    return status;
}

static void vec_discard_prepared(const vec_t *vec, void *value) {
    if (!value) return;
    (void)sequence_destroy_value(vec->element_type, value);
    sequence_deallocate(value);
}

static stl_status vec_initialize(vec_t *vec, const cmeta_type_desc *type,
                                             size_t elem_size, size_t elem_align, size_t limit) {
    stl_status status;
    size_t stride;
    uint64_t generation;
    if (!vec) return STL_INVALID_ARGUMENT;
    if (vec->initialized) return STL_INVALID_ARGUMENT;
    status = sequence_stride(elem_size, elem_align, &stride);
    if (status != STL_OK) return status;
    generation = vec->generation + UINT64_C(1);
    memset(vec, 0, sizeof(*vec));
    vec->elem_size = elem_size; vec->elem_stride = stride; vec->elem_align = elem_align;
    vec->element_limit = limit; vec->element_type = type; vec->generation = generation;
    vec->initialized = true;
    return STL_OK;
}

stl_status vec_init_bytes(vec_t *vec, size_t elem_size, size_t elem_align,
                                      size_t element_limit) {
    return vec_initialize(vec, NULL, elem_size, elem_align, element_limit);
}

stl_status vec_raw_init(vec_t *vec, const cmeta_type_desc *element_type,
                                size_t element_limit) {
    stl_status status;
    if (!vec || vec->initialized) return STL_INVALID_ARGUMENT;
    status = sequence_require_type(element_type, false);
    if (status != STL_OK) return status;
    return vec_initialize(vec, element_type, element_type->size, element_type->align,
                                element_limit);
}

stl_status vec_from_array_bytes(vec_t *vec, const void *elements, size_t count,
                                            size_t elem_size, size_t elem_align, size_t limit) {
    vec_t temporary = {0};
    stl_status status;
    size_t index;
    uint64_t generation;
    if (!vec) return STL_INVALID_ARGUMENT;
    if (vec->initialized) return STL_INVALID_ARGUMENT;
    if ((count && !elements) || count > limit)
        return count > limit ? STL_CAPACITY_EXCEEDED : STL_INVALID_ARGUMENT;
    status = vec_init_bytes(&temporary, elem_size, elem_align, limit);
    if (status != STL_OK) return status;
    for (index = 0u; index < count; ++index) {
        status = vec_push(&temporary, (const unsigned char *)elements + index * elem_size);
        if (status != STL_OK) { vec_raw_destroy_storage(&temporary); return status; }
    }
    generation = vec->generation + UINT64_C(1);
    temporary.generation = generation;
    *vec = temporary;
    return STL_OK;
}

stl_status vec_raw_from_array(vec_t *vec, const void *elements, size_t count,
                                      const cmeta_type_desc *type, size_t limit) {
    vec_t temporary = {0};
    stl_status status;
    size_t index;
    uint64_t generation;
    if (!vec) return STL_INVALID_ARGUMENT;
    if (vec->initialized) return STL_INVALID_ARGUMENT;
    if ((count && !elements) || count > limit)
        return count > limit ? STL_CAPACITY_EXCEEDED : STL_INVALID_ARGUMENT;
    status = vec_raw_init(&temporary, type, limit);
    if (status != STL_OK) return status;
    for (index = 0u; index < count; ++index) {
        status = vec_push(&temporary, (const unsigned char *)elements + index * type->size);
        if (status != STL_OK) { vec_raw_destroy_storage(&temporary); return status; }
    }
    generation = vec->generation + UINT64_C(1);
    temporary.generation = generation;
    *vec = temporary;
    return STL_OK;
}

void vec_raw_destroy_storage(vec_t *vec) {
    uint64_t generation;
    if (!vec) return;
    generation = vec->generation;
    if (vec->initialized) {
        (void)vec_clear(vec);
        generation = vec->generation + UINT64_C(1);
        sequence_deallocate(vec->data);
    }
    memset(vec, 0, sizeof(*vec));
    vec->generation = generation;
}

stl_status vec_clear(vec_t *vec) {
    size_t index;
    stl_status status;
    if (!vec_valid(vec)) return STL_INVALID_ARGUMENT;
    for (index = 0u; index < vec->size; ++index) {
        status = sequence_destroy_value(vec->element_type, vec_slot(vec, index));
        if (status != STL_OK) return status;
    }
    if (vec->size) { vec->size = 0u; ++vec->generation; }
    return STL_OK;
}

stl_status vec_reserve(vec_t *vec, size_t min_capacity) {
    bool changed;
    stl_status status = vec_grow_to(vec, min_capacity, &changed);
    if (status == STL_OK && changed) ++vec->generation;
    return status;
}

stl_status vec_resize(vec_t *vec, size_t new_size) {
    size_t old_size;
    stl_status status;
    if (!vec_valid(vec)) return STL_INVALID_ARGUMENT;
    if (new_size > vec->element_limit) return STL_CAPACITY_EXCEEDED;
    old_size = vec->size;
    if (new_size == old_size) return STL_OK;
    if (new_size < old_size) {
        while (vec->size > new_size) {
            --vec->size;
            status = sequence_destroy_value(vec->element_type, vec_slot(vec, vec->size));
            if (status != STL_OK) return status;
        }
        ++vec->generation;
        return STL_OK;
    }
    if (vec->element_type) return STL_TRAIT_MISSING;
    status = vec_grow_to(vec, new_size, NULL);
    if (status != STL_OK) return status;
    memset(vec_slot(vec, old_size), 0, (new_size - old_size) * vec->elem_stride);
    vec->size = new_size;
    ++vec->generation;
    return STL_OK;
}

stl_status vec_push(vec_t *vec, const void *elem) {
    void *prepared = NULL;
    stl_status status;
    if (!vec_valid(vec) || !elem) return STL_INVALID_ARGUMENT;
    if (vec->size >= vec->element_limit) return STL_CAPACITY_EXCEEDED;
    status = vec_prepare_copy(vec, elem, &prepared);
    if (status != STL_OK) return status;
    status = vec_grow_to(vec, vec->size + 1u, NULL);
    if (status != STL_OK) { vec_discard_prepared(vec, prepared); return status; }
    status = sequence_move_destroy(vec->element_type, vec->elem_size,
        vec_slot(vec, vec->size), prepared);
    sequence_deallocate(prepared);
    if (status != STL_OK) return status;
    ++vec->size; ++vec->generation;
    return STL_OK;
}

stl_status vec_pop(vec_t *vec, void *out_elem) {
    stl_status status;
    size_t index;
    if (!vec_valid(vec)) return STL_INVALID_ARGUMENT;
    if (!vec->size) return STL_EMPTY;
    index = vec->size - 1u;
    status = out_elem ? sequence_move_destroy(vec->element_type, vec->elem_size, out_elem,
                                                     vec_slot(vec, index))
                     : sequence_destroy_value(vec->element_type, vec_slot(vec, index));
    if (status != STL_OK) return status;
    vec->size = index; ++vec->generation;
    return STL_OK;
}

stl_status vec_insert(vec_t *vec, size_t index, const void *elem) {
    void *prepared = NULL;
    size_t cursor;
    stl_status status;
    if (!vec_valid(vec) || !elem || index > vec->size) return STL_INVALID_ARGUMENT;
    if (vec->size >= vec->element_limit) return STL_CAPACITY_EXCEEDED;
    status = vec_prepare_copy(vec, elem, &prepared);
    if (status != STL_OK) return status;
    status = vec_grow_to(vec, vec->size + 1u, NULL);
    if (status != STL_OK) { vec_discard_prepared(vec, prepared); return status; }
    if (!vec->element_type) {
        memmove(vec_slot(vec, index + 1u), vec_slot(vec, index),
                (vec->size - index) * vec->elem_stride);
    } else {
        for (cursor = vec->size; cursor > index; --cursor)
            (void)sequence_move_destroy(vec->element_type, vec->elem_size,
                vec_slot(vec, cursor), vec_slot(vec, cursor - 1u));
    }
    status = sequence_move_destroy(vec->element_type, vec->elem_size,
        vec_slot(vec, index), prepared);
    sequence_deallocate(prepared);
    if (status != STL_OK) return status;
    ++vec->size; ++vec->generation;
    return STL_OK;
}

stl_status vec_set(vec_t *vec, size_t index, const void *elem) {
    void *prepared = NULL;
    stl_status status;
    if (!vec_valid(vec) || !elem || index >= vec->size) return STL_INVALID_ARGUMENT;
    status = vec_prepare_copy(vec, elem, &prepared);
    if (status != STL_OK) return status;
    status = sequence_destroy_value(vec->element_type, vec_slot(vec, index));
    if (status == STL_OK)
        status = sequence_move_destroy(vec->element_type, vec->elem_size,
            vec_slot(vec, index), prepared);
    sequence_deallocate(prepared);
    if (status != STL_OK) return status;
    ++vec->generation;
    return STL_OK;
}

stl_status vec_erase(vec_t *vec, size_t index, void *out_elem) {
    size_t cursor;
    stl_status status;
    if (!vec_valid(vec) || index >= vec->size) return STL_INVALID_ARGUMENT;
    status = out_elem ? sequence_move_destroy(vec->element_type, vec->elem_size, out_elem,
                                                     vec_slot(vec, index))
                     : sequence_destroy_value(vec->element_type, vec_slot(vec, index));
    if (status != STL_OK) return status;
    if (!vec->element_type) {
        memmove(vec_slot(vec, index), vec_slot(vec, index + 1u),
                (vec->size - index - 1u) * vec->elem_stride);
    } else {
        for (cursor = index; cursor + 1u < vec->size; ++cursor)
            (void)sequence_move_destroy(vec->element_type, vec->elem_size,
                vec_slot(vec, cursor), vec_slot(vec, cursor + 1u));
    }
    --vec->size; ++vec->generation;
    return STL_OK;
}

stl_status vec_swap_remove(vec_t *vec, size_t index, void *out_elem) {
    stl_status status;
    size_t last;
    if (!vec_valid(vec) || index >= vec->size) return STL_INVALID_ARGUMENT;
    last = vec->size - 1u;
    status = out_elem ? sequence_move_destroy(vec->element_type, vec->elem_size, out_elem,
                                                     vec_slot(vec, index))
                     : sequence_destroy_value(vec->element_type, vec_slot(vec, index));
    if (status != STL_OK) return status;
    if (index != last) {
        status = sequence_move_destroy(vec->element_type, vec->elem_size,
            vec_slot(vec, index), vec_slot(vec, last));
        if (status != STL_OK) return status;
    }
    --vec->size; ++vec->generation;
    return STL_OK;
}

void *vec_at(vec_t *vec, size_t index) {
    return vec_valid(vec) && index < vec->size ? vec_slot(vec, index) : NULL;
}
const void *vec_at_const(const vec_t *vec, size_t index) {
    return vec_valid(vec) && index < vec->size ? vec_slot_const(vec, index) : NULL;
}
void *vec_data(vec_t *vec) { return vec_valid(vec) ? vec->data : NULL; }
const void *vec_data_const(const vec_t *vec) { return vec_valid(vec) ? vec->data : NULL; }
size_t vec_size(const vec_t *vec) { return vec_valid(vec) ? vec->size : 0u; }
size_t vec_capacity(const vec_t *vec) { return vec_valid(vec) ? vec->capacity : 0u; }
uint64_t vec_generation(const vec_t *vec) { return vec ? vec->generation : UINT64_C(0); }
bool vec_empty(const vec_t *vec) { return vec_size(vec) == 0u; }
