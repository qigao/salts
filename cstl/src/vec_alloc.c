#include <cstl/vec_alloc.h>
#include "sequence_internal.h"

/* Receipts stay with the owner: no foreign allocation header is installed in
 * vec_t, and every callback release receives its original pointer and charge. */
typedef struct vec_alloc_block {
    void *raw;
    void *data;
    size_t bytes;
} vec_alloc_block;

struct vec_alloc {
    vec_t view;
    stl_allocator allocator;
    vec_alloc_block backing;
};

enum { VEC_ALLOC_MIN_CAPACITY = 8 };

static void block_drop(const stl_allocator *allocator, vec_alloc_block *block) {
    if (block->raw != NULL)
        allocator->deallocate(allocator->context, block->raw, block->bytes);
    *block = (vec_alloc_block){0};
}

static stl_status block_new(const stl_allocator *allocator, size_t count,
                             size_t stride, size_t alignment, vec_alloc_block *out) {
    size_t bytes;
    uintptr_t address;
    stl_status status = sequence_bytes(count, stride, &bytes);
    *out = (vec_alloc_block){0};
    if (status != STL_OK) return status;
    if (bytes == 0u) return STL_OK;
    if (bytes > SIZE_MAX - (alignment - 1u)) return STL_CAPACITY_EXCEEDED;
    bytes += alignment - 1u;
    status = allocator->allocate(allocator->context, bytes, &out->raw);
    if (status != STL_OK) return status;
    if (out->raw == NULL) return STL_INVALID_ARGUMENT;
    out->bytes = bytes;
    address = (uintptr_t)out->raw;
    if (address > UINTPTR_MAX - (alignment - 1u)) {
        block_drop(allocator, out);
        return STL_CAPACITY_EXCEEDED;
    }
    out->data = (void *)((address + alignment - 1u) & ~(uintptr_t)(alignment - 1u));
    return STL_OK;
}

static stl_status owner_new(const cmeta_type_desc *type, size_t size, size_t alignment,
                             size_t limit, const stl_allocator *allocator, vec_alloc_t **out) {
    vec_t view = {0};
    vec_alloc_t *owner;
    void *storage = NULL;
    stl_status status;
    if (out == NULL) return STL_INVALID_ARGUMENT;
    *out = NULL;
    if (allocator == NULL || allocator->allocate == NULL || allocator->deallocate == NULL)
        return STL_INVALID_ARGUMENT;
    status = type != NULL ? vec_raw_init(&view, type, limit)
                          : vec_init_bytes(&view, size, alignment, limit);
    if (status != STL_OK) return status;
    status = allocator->allocate(allocator->context, sizeof(*owner), &storage);
    if (status != STL_OK) return status;
    if (storage == NULL) return STL_INVALID_ARGUMENT;
    owner = (vec_alloc_t *)storage;
    *owner = (vec_alloc_t){view, *allocator, {0}};
    *out = owner;
    return STL_OK;
}

stl_status vec_alloc_new(const cmeta_type_desc *type, size_t limit,
                         const stl_allocator *allocator, vec_alloc_t **out) {
    if (type == NULL) {
        if (out != NULL) *out = NULL;
        return STL_INVALID_ARGUMENT;
    }
    return owner_new(type, 0u, 0u, limit, allocator, out);
}

stl_status vec_alloc_new_bytes(size_t size, size_t alignment, size_t limit,
                               const stl_allocator *allocator, vec_alloc_t **out) {
    return owner_new(NULL, size, alignment, limit, allocator, out);
}

static unsigned char *slot(vec_t *view, size_t index) {
    return (unsigned char *)view->data + index * view->elem_stride;
}

static stl_status grow(vec_alloc_t *owner, size_t minimum) {
    vec_t *view = &owner->view;
    vec_alloc_block replacement;
    size_t capacity, i;
    stl_status status;
    if (minimum > view->element_limit) return STL_CAPACITY_EXCEEDED;
    if (minimum <= view->capacity) return STL_OK;
    capacity = view->capacity != 0u ? view->capacity : VEC_ALLOC_MIN_CAPACITY;
    if (capacity > view->element_limit) capacity = view->element_limit;
    while (capacity < minimum) {
        if (capacity > view->element_limit - capacity) {
            capacity = view->element_limit;
            break;
        }
        capacity *= 2u;
    }
    status = block_new(&owner->allocator, capacity, view->elem_stride,
                       view->elem_align, &replacement);
    if (status != STL_OK) return status;
    /* All fallible work precedes relocation. Native type metadata is borrowed
     * immutable; move/destroy were admitted by vec_raw_init and cannot fail. */
    for (i = 0u; i < view->size; ++i)
        (void)sequence_move_destroy(view->element_type, view->elem_size,
            (unsigned char *)replacement.data + i * view->elem_stride, slot(view, i));
    block_drop(&owner->allocator, &owner->backing);
    owner->backing = replacement;
    view->data = replacement.data;
    view->capacity = capacity;
    return STL_OK;
}

stl_status vec_alloc_push(vec_alloc_t *owner, const void *element) {
    vec_t *view;
    vec_alloc_block snapshot;
    stl_status status;
    if (owner == NULL || element == NULL) return STL_INVALID_ARGUMENT;
    view = &owner->view;
    if (view->size >= view->element_limit) return STL_CAPACITY_EXCEEDED;
    status = block_new(&owner->allocator, 1u, view->elem_stride, view->elem_align, &snapshot);
    if (status != STL_OK) return status;
    status = sequence_copy(view->element_type, view->elem_size, snapshot.data, element);
    if (status != STL_OK) {
        block_drop(&owner->allocator, &snapshot);
        return status;
    }
    status = grow(owner, view->size + 1u);
    if (status == STL_OK) {
        (void)sequence_move_destroy(view->element_type, view->elem_size,
                                    slot(view, view->size), snapshot.data);
        ++view->size;
        ++view->generation;
    } else {
        (void)sequence_destroy_value(view->element_type, snapshot.data);
    }
    block_drop(&owner->allocator, &snapshot);
    return status;
}

stl_status vec_alloc_resize(vec_alloc_t *owner, size_t new_size) {
    vec_t *view;
    stl_status status;
    if (owner == NULL) return STL_INVALID_ARGUMENT;
    view = &owner->view;
    if (new_size <= view->size) return vec_resize(view, new_size);
    if (new_size > view->element_limit) return STL_CAPACITY_EXCEEDED;
    if (view->element_type != NULL) return STL_TRAIT_MISSING;
    status = grow(owner, new_size);
    if (status != STL_OK) return status;
    memset(slot(view, view->size), 0, (new_size - view->size) * view->elem_stride);
    view->size = new_size;
    ++view->generation;
    return STL_OK;
}

const vec_t *vec_alloc_view(const vec_alloc_t *owner) {
    return owner != NULL ? &owner->view : NULL;
}

void *vec_alloc_at(vec_alloc_t *owner, size_t index) {
    return owner != NULL ? vec_at(&owner->view, index) : NULL;
}

void vec_alloc_destroy(vec_alloc_t *owner) {
    stl_allocator allocator;
    if (owner == NULL) return;
    allocator = owner->allocator;
    (void)vec_clear(&owner->view);
    block_drop(&allocator, &owner->backing);
    allocator.deallocate(allocator.context, owner, sizeof(*owner));
}
