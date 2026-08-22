#ifndef TURBOSTL_SORT_H
#define TURBOSTL_SORT_H

#include <turbostl/status.h>

#include <cmeta/cmeta.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stable in-place sort of typed or raw contiguous elements. */
stl_status stable_sort(void *base, size_t count,
                       const cmeta_type_desc *element_type,
                       int (*compare)(const void *left, const void *right,
                                      void *context),
                       void *context);

/* Temporary repository-migration alias. */
#define turbo_stable_sort stable_sort

#ifdef __cplusplus
}
#endif

#endif /* TURBOSTL_SORT_H */
