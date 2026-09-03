#ifndef SALTS_STL_STATUS_INTERNAL_H
#define SALTS_STL_STATUS_INTERNAL_H

#include "salts_error.h"
#include <cstl/status.h>

static inline int salts_core_status_from_stl(stl_status status) {
  switch (status) {
    case STL_OK: return SALTS_OK;
    case STL_INVALID_ARGUMENT: return SALTS_EINVAL;
    case STL_OUT_OF_MEMORY: return SALTS_ENOMEM;
    case STL_CAPACITY_EXCEEDED: return SALTS_ERANGE;
    case STL_EMPTY:
    case STL_NOT_FOUND:
    case STL_TYPE_MISMATCH:
    case STL_TRAIT_MISSING: return SALTS_EIO;
  }
  return SALTS_EIO;
}

#endif
