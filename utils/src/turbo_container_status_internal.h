#ifndef TURBO_CONTAINER_STATUS_INTERNAL_H
#define TURBO_CONTAINER_STATUS_INTERNAL_H

#include "turbo_error.h"
#include <turbo/container/status.h>

static inline int turbo_core_status_from_container(container_status status) {
  switch (status) {
    case CONTAINER_OK: return TURBO_OK;
    case CONTAINER_INVALID_ARGUMENT: return TURBO_EINVAL;
    case CONTAINER_OUT_OF_MEMORY: return TURBO_ENOMEM;
    case CONTAINER_CAPACITY_EXCEEDED: return TURBO_ERANGE;
    case CONTAINER_EMPTY:
    case CONTAINER_NOT_FOUND:
    case CONTAINER_TYPE_MISMATCH:
    case CONTAINER_TRAIT_MISSING: return TURBO_EIO;
  }
  return TURBO_EIO;
}

#endif
