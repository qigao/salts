#ifndef SALTS_VEC_MEMORY_H
#define SALTS_VEC_MEMORY_H

#include <cstl/allocator.h>
#include "sequence_internal.h"

/* NULL is the original ordinary-Vec allocation policy. A bound owner always
 * supplies its copied callbacks. This selection never changes after failure.
 */
typedef struct vec_memory_receipt { void *raw; size_t bytes; } vec_memory_receipt;

static inline stl_status vec_memory_allocate(const stl_allocator *allocator,
    size_t count, size_t stride, size_t alignment, void **out_data) {
    size_t bytes, overhead;
    uintptr_t address, aligned;
    void *raw = NULL;
    stl_status status;
    vec_memory_receipt receipt;

    if (allocator == NULL)
        return sequence_allocate(count, stride, alignment, out_data);
    if (out_data == NULL || !sequence_alignment_valid(alignment))
        return STL_INVALID_ARGUMENT;
    *out_data = NULL;
    status = sequence_bytes(count, stride, &bytes);
    if (status != STL_OK || bytes == 0u) return status;
    if (alignment - 1u > SIZE_MAX - sizeof(receipt)) return STL_CAPACITY_EXCEEDED;
    overhead = sizeof(receipt) + alignment - 1u;
    if (bytes > SIZE_MAX - overhead) return STL_CAPACITY_EXCEEDED;
    bytes += overhead;
    status = allocator->allocate(allocator->context, bytes, &raw);
    if (status != STL_OK) return status;
    if (raw == NULL) return STL_OUT_OF_MEMORY;
    if ((uintptr_t)raw > UINTPTR_MAX - overhead) {
        allocator->deallocate(allocator->context, raw, bytes);
        return STL_CAPACITY_EXCEEDED;
    }
    address = (uintptr_t)raw + sizeof(receipt);
    aligned = (address + alignment - 1u) & ~(uintptr_t)(alignment - 1u);
    receipt.raw = raw;
    receipt.bytes = bytes;
    /* memcpy also supports byte-aligned payloads without unaligned receipt access. */
    memcpy((unsigned char *)(void *)aligned - sizeof(receipt), &receipt, sizeof(receipt));
    *out_data = (void *)aligned;
    return STL_OK;
}

static inline void vec_memory_deallocate(const stl_allocator *allocator, void *data) {
    vec_memory_receipt receipt;
    if (allocator == NULL) { sequence_deallocate(data); return; }
    if (data == NULL) return;
    memcpy(&receipt, (unsigned char *)data - sizeof(receipt), sizeof(receipt));
    allocator->deallocate(allocator->context, receipt.raw, receipt.bytes);
}

#endif /* SALTS_VEC_MEMORY_H */
