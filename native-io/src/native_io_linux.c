#include "native_io_internal.h"

#include <turbo/error_codes.h>

int turbo_io_epoll_backend_init(turbo_io_backend *backend, const turbo_io_backend_config *config);
int turbo_io_uring_backend_init(turbo_io_backend *backend, const turbo_io_backend_config *config);

bool turbo_io_platform_backend_supported(turbo_io_backend_kind kind) {
  return kind == TURBO_IO_BACKEND_EPOLL || kind == TURBO_IO_BACKEND_IO_URING;
}

bool turbo_io_platform_pipe_supported(turbo_io_backend_kind kind) {
  (void)kind;
  return false;
}

int turbo_io_platform_backend_init(turbo_io_backend *backend,
                                   const turbo_io_backend_config *config) {
  if (config->kind == TURBO_IO_BACKEND_EPOLL) return turbo_io_epoll_backend_init(backend, config);
  if (config->kind == TURBO_IO_BACKEND_IO_URING)
    return turbo_io_uring_backend_init(backend, config);
  return TURBO_ENOTSUP;
}
