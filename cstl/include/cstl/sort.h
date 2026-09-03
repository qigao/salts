#ifndef CSTL_SORT_H
#define CSTL_SORT_H

#include <cstl/status.h>

#include <cmeta/cmeta.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stable O(n log n) sort. The type descriptor is borrowed for the call and
 * must provide COMPARE, COPY, MOVE, and DESTROY. scratch_byte_limit bounds
 * the single temporary value array; insufficient space leaves base intact. */
stl_status stable_sort(void *base, size_t count,
                       const cmeta_type_desc *type,
                       size_t scratch_byte_limit);


#ifdef __cplusplus
}
#endif

#endif /* CSTL_SORT_H */
