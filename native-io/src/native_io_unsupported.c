#include "native_io_internal.h"

#include <turbo/error_codes.h>

bool turbo_io_platform_backend_supported(turbo_io_backend_kind kind) {
  (void)kind;
  return false;
}

bool turbo_io_platform_pipe_supported(turbo_io_backend_kind kind) {
  (void)kind;
  return false;
}

int turbo_io_platform_backend_init(turbo_io_backend *backend,
                                   const turbo_io_backend_config *config) {
  (void)backend;
  (void)config;
  return TURBO_ENOTSUP;
}
