#ifndef TURBO_STL_SORT_H
#define TURBO_STL_SORT_H

#include <turbo/stl/export.h>
#include <turbo/stl/status.h>

#include <cmeta/cmeta.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stable O(n log n) sort. The type descriptor is borrowed for the call and
 * must provide COMPARE, COPY, MOVE, and DESTROY. scratch_byte_limit bounds
 * the single temporary value array; insufficient space leaves base intact. */
TURBO_STL_API turbo_stl_status turbo_stable_sort(
    void *base, size_t count, const cmeta_type_desc *type,
    size_t scratch_byte_limit);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_STL_SORT_H */
