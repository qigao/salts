#include <turbo/native_io.h>

int main() {
  const turbo_io_endpoint endpoint{1u, 1u};
  const turbo_io_request request{1u, 1u};
  return turbo_io_endpoint_valid(endpoint) && turbo_io_request_valid(request) &&
                 turbo_io_backend_model(TURBO_IO_BACKEND_IOCP) == TURBO_IO_MODEL_COMPLETION &&
                 turbo_io_backend_model(TURBO_IO_BACKEND_EPOLL) == TURBO_IO_MODEL_READINESS &&
                 turbo_io_backend_model(TURBO_IO_BACKEND_IO_URING) == TURBO_IO_MODEL_COMPLETION &&
                 turbo_io_backend_model(TURBO_IO_BACKEND_KQUEUE) == TURBO_IO_MODEL_READINESS
             ? 0
             : 1;
}
