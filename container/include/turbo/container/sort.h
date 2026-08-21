#ifndef TURBO_CONTAINER_SORT_H
#define TURBO_CONTAINER_SORT_H

#include <turbo/container/export.h>
#include <turbo/container/status.h>

#include <cmeta/cmeta.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stable O(n log n) sort. The type descriptor is borrowed for the call and
 * must provide COMPARE, COPY, MOVE, and DESTROY. scratch_byte_limit bounds
 * the single temporary value array; insufficient space leaves base intact. */
CONTAINER_API container_status turbo_stable_sort(
    void *base, size_t count, const cmeta_type_desc *type,
    size_t scratch_byte_limit);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_CONTAINER_SORT_H */
