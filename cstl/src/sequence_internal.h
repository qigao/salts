#ifndef SALTS_SEQUENCE_INTERNAL_H
#define SALTS_SEQUENCE_INTERNAL_H

#include <cstl/status.h>
#include <cmeta/cmeta.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static inline bool sequence_alignment_valid(size_t alignment) {
    return alignment != 0u && (alignment & (alignment - 1u)) == 0u;
}

static inline stl_status sequence_stride(size_t elem_size, size_t elem_align,
                                                     size_t *out_stride) {
    size_t padding;

    if (elem_size == 0u || !sequence_alignment_valid(elem_align) || !out_stride)
        return STL_INVALID_ARGUMENT;
    padding = elem_align - 1u;
    if (elem_size > SIZE_MAX - padding)
        return STL_CAPACITY_EXCEEDED;
    *out_stride = (elem_size + padding) & ~padding;
    return STL_OK;
}

static inline stl_status sequence_bytes(size_t count, size_t stride,
                                                    size_t *out_bytes) {
    if (!out_bytes || (count != 0u && stride > SIZE_MAX / count))
        return STL_CAPACITY_EXCEEDED;
    *out_bytes = count * stride;
    return STL_OK;
}

static inline stl_status sequence_allocate(size_t count, size_t stride,
                                                        size_t alignment, void **out_data) {
    size_t bytes;
    size_t overhead;
    void *raw;
    uintptr_t address;
    uintptr_t aligned;

    if (!out_data || !sequence_alignment_valid(alignment))
        return STL_INVALID_ARGUMENT;
    *out_data = NULL;
    if (sequence_bytes(count, stride, &bytes) != STL_OK)
        return STL_CAPACITY_EXCEEDED;
    if (bytes == 0u)
        return STL_OK;
    if (alignment - 1u > SIZE_MAX - sizeof(void *))
        return STL_CAPACITY_EXCEEDED;
    overhead = sizeof(void *) + alignment - 1u;
    if (bytes > SIZE_MAX - overhead)
        return STL_CAPACITY_EXCEEDED;
    raw = malloc(bytes + overhead);
    if (!raw)
        return STL_OUT_OF_MEMORY;
    address = (uintptr_t)raw + sizeof(void *);
    if (address > UINTPTR_MAX - (alignment - 1u)) {
        free(raw);
        return STL_CAPACITY_EXCEEDED;
    }
    aligned = (address + alignment - 1u) & ~(uintptr_t)(alignment - 1u);
    ((void **)(void *)aligned)[-1] = raw;
    *out_data = (void *)aligned;
    return STL_OK;
}

static inline void sequence_deallocate(void *data) {
    if (data)
        free(((void **)data)[-1]);
}

static inline stl_status sequence_require_type(const cmeta_type_desc *type,
                                                            bool require_compare) {
    cmeta_trait_flags required = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY;

    if (!type || type->size == 0u || !sequence_alignment_valid(type->align))
        return STL_INVALID_ARGUMENT;
    if (require_compare)
        required |= CMETA_TRAIT_COMPARE;
    return cmeta_type_require_traits(type, required) == CMETA_OK ? STL_OK
                                                                   : STL_TRAIT_MISSING;
}

static inline stl_status sequence_copy(const cmeta_type_desc *type,
                                                    size_t elem_size, void *destination,
                                                    const void *source) {
    if (!destination || !source)
        return STL_INVALID_ARGUMENT;
    if (!type) {
        memcpy(destination, source, elem_size);
        return STL_OK;
    }
    if (cmeta_type_require_traits(type, CMETA_TRAIT_COPY) != CMETA_OK)
        return STL_TRAIT_MISSING;
    return type->traits->copy_construct(destination, source) ? STL_OK
                                                               : STL_OUT_OF_MEMORY;
}

static inline stl_status sequence_move_destroy(const cmeta_type_desc *type,
                                                            size_t elem_size, void *destination,
                                                            void *source) {
    if (!destination || !source)
        return STL_INVALID_ARGUMENT;
    if (!type) {
        memcpy(destination, source, elem_size);
        return STL_OK;
    }
    if (cmeta_type_require_traits(type, CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY) != CMETA_OK)
        return STL_TRAIT_MISSING;
    type->traits->move_construct(destination, source);
    type->traits->destroy(source);
    return STL_OK;
}

static inline stl_status sequence_destroy_value(const cmeta_type_desc *type,
                                                             void *value) {
    if (!value || !type)
        return STL_OK;
    if (cmeta_type_require_traits(type, CMETA_TRAIT_DESTROY) != CMETA_OK)
        return STL_TRAIT_MISSING;
    type->traits->destroy(value);
    return STL_OK;
}

#endif /* SALTS_SEQUENCE_INTERNAL_H */
