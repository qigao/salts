#ifndef TURBO_STL_STATUS_INTERNAL_H
#define TURBO_STL_STATUS_INTERNAL_H

#include "turbo_error.h"
#include <turbo/stl/status.h>

static inline int turbo_core_status_from_stl(turbo_stl_status status) {
  switch (status) {
    case TURBO_STL_OK: return TURBO_OK;
    case TURBO_STL_INVALID_ARGUMENT: return TURBO_EINVAL;
    case TURBO_STL_OUT_OF_MEMORY: return TURBO_ENOMEM;
    case TURBO_STL_CAPACITY_EXCEEDED: return TURBO_ERANGE;
    case TURBO_STL_EMPTY:
    case TURBO_STL_NOT_FOUND:
    case TURBO_STL_TYPE_MISMATCH:
    case TURBO_STL_TRAIT_MISSING: return TURBO_EIO;
  }
  return TURBO_EIO;
}

#endif
