#include "native_io_internal.h"

#include <turbo/error_codes.h>

int turbo_io_kqueue_backend_init(turbo_io_backend *backend, const turbo_io_backend_config *config);

bool turbo_io_platform_backend_supported(turbo_io_backend_kind kind) {
  return kind == TURBO_IO_BACKEND_KQUEUE;
}

bool turbo_io_platform_pipe_supported(turbo_io_backend_kind kind) {
  return kind == TURBO_IO_BACKEND_KQUEUE;
}

int turbo_io_platform_backend_init(turbo_io_backend *backend,
                                   const turbo_io_backend_config *config) {
  if (config->kind == TURBO_IO_BACKEND_KQUEUE) return turbo_io_kqueue_backend_init(backend, config);
  return TURBO_ENOTSUP;
}
