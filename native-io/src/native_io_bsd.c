#include "native_io_internal.h"

#include <turbo/error_codes.h>

int turbo_io_kqueue_backend_init(native_io_backend *backend, const native_io_backend_config *config);

bool native_io_platform_backend_supported(native_io_backend_kind kind) {
  return kind == NATIVE_IO_BACKEND_KQUEUE;
}

bool native_io_platform_pipe_supported(native_io_backend_kind kind) {
  return kind == NATIVE_IO_BACKEND_KQUEUE;
}

int native_io_platform_backend_init(native_io_backend *backend,
                                   const native_io_backend_config *config) {
  if (config->kind == NATIVE_IO_BACKEND_KQUEUE) return turbo_io_kqueue_backend_init(backend, config);
  return TURBO_ENOTSUP;
}
