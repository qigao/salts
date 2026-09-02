#ifndef TURBO_STL_STATUS_INTERNAL_H
#define TURBO_STL_STATUS_INTERNAL_H

#include "turbo_error.h"
#include <rocida/stl/status.h>

static inline int turbo_core_status_from_stl(stl_status status) {
  switch (status) {
    case STL_OK: return TURBO_OK;
    case STL_INVALID_ARGUMENT: return TURBO_EINVAL;
    case STL_OUT_OF_MEMORY: return TURBO_ENOMEM;
    case STL_CAPACITY_EXCEEDED: return TURBO_ERANGE;
    case STL_EMPTY:
    case STL_NOT_FOUND:
    case STL_TYPE_MISMATCH:
    case STL_TRAIT_MISSING: return TURBO_EIO;
  }
  return TURBO_EIO;
}

#endif
