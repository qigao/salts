#include <turbostl/sort.h>

#include "sequence_internal.h"

#include <stdint.h>
#include <string.h>

static unsigned char *sort_slot(void *base, size_t stride, size_t index) {
    return (unsigned char *)base + stride * index;
}

static void sort_destroy_range(const cmeta_type_desc *type, void *base,
                                     size_t stride, size_t count) {
    size_t index;
    for (index = 0u; index < count; ++index)
        type->traits->destroy(sort_slot(base, stride, index));
}

static void sort_merge(const cmeta_type_desc *type, void *source,
                             void *destination, size_t stride, size_t left,
                             size_t middle, size_t right) {
    size_t first = left;
    size_t second = middle;
    size_t output = left;

    while (first < middle && second < right) {
        size_t selected;
        if (type->traits->compare(sort_slot(source, stride, first),
                                  sort_slot(source, stride, second)) <= 0)
            selected = first++;
        else
            selected = second++;
        type->traits->move_construct(sort_slot(destination, stride, output++),
                                     sort_slot(source, stride, selected));
        type->traits->destroy(sort_slot(source, stride, selected));
    }
    while (first < middle) {
        type->traits->move_construct(sort_slot(destination, stride, output++),
                                     sort_slot(source, stride, first));
        type->traits->destroy(sort_slot(source, stride, first++));
    }
    while (second < right) {
        type->traits->move_construct(sort_slot(destination, stride, output++),
                                     sort_slot(source, stride, second));
        type->traits->destroy(sort_slot(source, stride, second++));
    }
}

turbostl_status turbo_stable_sort(void *base, size_t count,
                                   const cmeta_type_desc *type,
                                   size_t scratch_byte_limit) {
    const cmeta_trait_flags required =
        CMETA_TRAIT_COMPARE | CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
        CMETA_TRAIT_DESTROY;
    turbostl_status status;
    size_t stride;
    size_t bytes;
    size_t copied = 0u;
    size_t width;
    size_t left;
    void *scratch = NULL;
    void *source;
    void *destination;

    if (type == NULL || type->size == 0u ||
        !sequence_alignment_valid(type->align))
        return TURBO_STL_INVALID_ARGUMENT;
    if (cmeta_type_require_traits(type, required) != CMETA_OK)
        return TURBO_STL_TRAIT_MISSING;
    if (count == 0u)
        return TURBO_STL_OK;
    if (base == NULL)
        return TURBO_STL_INVALID_ARGUMENT;
    if ((uintptr_t)base % type->align != 0u)
        return TURBO_STL_INVALID_ARGUMENT;
    if (count == 1u)
        return TURBO_STL_OK;
    status = sequence_stride(type->size, type->align, &stride);
    if (status != TURBO_STL_OK)
        return status;
    status = sequence_bytes(count, stride, &bytes);
    if (status != TURBO_STL_OK)
        return status;
    if (bytes > scratch_byte_limit)
        return TURBO_STL_CAPACITY_EXCEEDED;
    status = sequence_allocate(count, stride, type->align, &scratch);
    if (status != TURBO_STL_OK)
        return status;
    for (copied = 0u; copied < count; ++copied) {
        if (!type->traits->copy_construct(sort_slot(scratch, stride, copied),
                                          sort_slot(base, stride, copied))) {
            sort_destroy_range(type, scratch, stride, copied);
            sequence_deallocate(scratch);
            return TURBO_STL_OUT_OF_MEMORY;
        }
    }

    sort_destroy_range(type, base, stride, count);
    source = scratch;
    destination = base;
    for (width = 1u; width < count;) {
        for (left = 0u; left < count; left += width * 2u) {
            size_t middle = left + width < count ? left + width : count;
            size_t right = middle + width < count ? middle + width : count;
            sort_merge(type, source, destination, stride, left, middle, right);
        }
        {
            void *swap = source;
            source = destination;
            destination = swap;
        }
        if (width > count / 2u)
            width = count;
        else
            width *= 2u;
    }
    if (source != base) {
        size_t index;
        for (index = 0u; index < count; ++index) {
            type->traits->move_construct(sort_slot(base, stride, index),
                                         sort_slot(source, stride, index));
            type->traits->destroy(sort_slot(source, stride, index));
        }
    }
    sequence_deallocate(scratch);
    return TURBO_STL_OK;
}
