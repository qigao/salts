#ifndef CSTL_ALLOCATOR_H
#define CSTL_ALLOCATOR_H

#include <cstl/status.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A requested-byte allocation contract, not a process-global allocator.
 * allocate receives nonzero bytes and must return malloc-aligned storage on
 * STL_OK. On failure it publishes NULL and retains nothing. deallocate receives
 * the original pointer and exact requested byte count, once per allocation.
 * Both callbacks are mandatory. Their descriptor is copied by owners, but the
 * context remains borrowed through final deallocation. A supplied allocator is
 * never replaced on failure. Callbacks must not reenter the same owner.
 * Synchronization and allocations made inside element traits belong to callers.
 */
typedef struct stl_allocator {
    void *context;
    stl_status (*allocate)(void *context, size_t bytes, void **out);
    void (*deallocate)(void *context, void *data, size_t bytes);
} stl_allocator;

#ifdef __cplusplus
}
#endif

#endif /* CSTL_ALLOCATOR_H */
