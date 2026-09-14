#ifndef CSTL_VEC_ALLOC_H
#define CSTL_VEC_ALLOC_H

#include <cstl/allocator.h>
#include <cstl/vec.h>

#ifdef __cplusplus
extern "C" {
#endif

/* An allocator-bound owner over the ordinary Vec engine. vec_t's ABI is
 * unchanged; only this opaque owner may mutate or release its storage.
 * Single-threaded: externally synchronize all access and quiesce before destroy.
 * The allocator must be non-NULL. Type/trait metadata is immutable and borrowed
 * for the owner's lifetime. An element limit of zero admits an empty owner.
 * Creation sets *out to NULL on failure; pass an empty output slot.
 * Owner, backing storage, alignment overhead and staging copies are all charged
 * to the selected allocator. Growth accounts for simultaneous old/new storage.
 * Payload allocated by user-supplied element traits is outside this byte charge.
 */
typedef struct vec_alloc vec_alloc_t;

stl_status vec_alloc_new(const cmeta_type_desc *element_type, size_t element_limit,
                         const stl_allocator *allocator, vec_alloc_t **out);
stl_status vec_alloc_new_bytes(size_t elem_size, size_t elem_align,
                               size_t element_limit, const stl_allocator *allocator,
                               vec_alloc_t **out);

/* Failed push/resize preserves values, size, capacity, generation and storage.
 * Push accepts a source element in this vector and stages it before relocation.
 * Byte growth is zero-filled. Typed growth requires a constructor not present
 * in the current Vec contract and returns STL_TRAIT_MISSING; shrinking destroys
 * removed elements. Capacity/allocator errors are returned without fallback.
 * Like ordinary Vec: amortized O(1) push, O(n) relocation/resize/destroy.
 */
stl_status vec_alloc_push(vec_alloc_t *owner, const void *element);
stl_status vec_alloc_resize(vec_alloc_t *owner, size_t size);

/* Borrowed element pointers expire on relocation, shrink past the element, or
 * destroy. The const Vec view lives until destroy, reflects later mutations,
 * and must never be cast to mutable for ordinary Vec mutation/destruction.
 * NULL owner gives a NULL view/element; an out-of-range index gives NULL.
 */
void *vec_alloc_at(vec_alloc_t *owner, size_t index);
const vec_t *vec_alloc_view(const vec_alloc_t *owner);
void vec_alloc_destroy(vec_alloc_t *owner); /* NULL is a no-op. */

#ifdef __cplusplus
}
#endif

#endif /* CSTL_VEC_ALLOC_H */
