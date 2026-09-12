#ifndef CSTL_ALLOCATOR_H
#define CSTL_ALLOCATOR_H

#include <cstl/status.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* An explicit allocation domain, not a process-global allocator. Callbacks are
 * copied by storage owners; context is borrowed through their final release.
 * allocate receives positive bytes and must either return STL_OK with storage
 * aligned for ordinary C objects, or fail with *out == NULL and no live charge.
 * deallocate receives the exact original pointer and requested byte count.
 * Callbacks must not reenter their owning container. Thread safety belongs to
 * the caller; no implicit default allocator or allocation-domain fallback. */
typedef struct stl_allocator {
    void *context;
    stl_status (*allocate)(void *context, size_t bytes, void **out);
    void (*deallocate)(void *context, void *data, size_t bytes);
} stl_allocator;

#ifdef __cplusplus
}
#endif
#endif /* CSTL_ALLOCATOR_H */
