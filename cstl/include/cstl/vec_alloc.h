#ifndef CSTL_VEC_ALLOC_H
#define CSTL_VEC_ALLOC_H

#include <cstl/allocator.h>
#include <cstl/vec.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vec_alloc vec_alloc_t;

/* Single-owner, allocator-bound Vec. The allocator is required and copied;
 * its context and typed element metadata must remain valid and immutable until
 * destroy. Constructors clear *out on failure. Zero element_limit is valid.
 * All owner, backing, alignment-slack and push-snapshot bytes use this allocator.
 * Allocations performed inside element callbacks remain the element's domain.
 * Existing vec_t layout and allocation behavior are unchanged. */
stl_status vec_alloc_new(const cmeta_type_desc *element_type, size_t element_limit,
                         const stl_allocator *allocator, vec_alloc_t **out);
stl_status vec_alloc_new_bytes(size_t elem_size, size_t elem_align, size_t element_limit,
                               const stl_allocator *allocator, vec_alloc_t **out);

/* Failure preserves data, size, capacity and generation. push snapshots its
 * input before growth, including input borrowed from this vector. Copy failure
 * is STL_OUT_OF_MEMORY, as in vec_push; allocator errors propagate unchanged.
 * Capacity is element-bounded; peak charges include old/new backing overlap.
 * push is amortized O(1), growth O(size); temporary storage is O(capacity).
 * resize zero-fills byte storage; typed growth requires a constructor and is
 * rejected with STL_TRAIT_MISSING, matching vec_resize. Shrink destroys tails. */
stl_status vec_alloc_push(vec_alloc_t *owner, const void *element);
stl_status vec_alloc_resize(vec_alloc_t *owner, size_t new_size);

/* Borrowed views expire on successful mutation or destroy. Use only const Vec
 * operations on view; never cast away const to mutate/free allocator storage.
 * at returns NULL outside the live range. NULL owner yields NULL in both APIs.
 * Direct element writes require exclusive access and must preserve lifecycle. */
const vec_t *vec_alloc_view(const vec_alloc_t *owner);
void *vec_alloc_at(vec_alloc_t *owner, size_t index);
void vec_alloc_destroy(vec_alloc_t *owner); /* NULL accepted. */

#ifdef __cplusplus
}
#endif
#endif /* CSTL_VEC_ALLOC_H */
