#include <turbo/native_io.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

int main() {
  const turbo_io_endpoint endpoint{1u, 1u};
  const turbo_io_request request{1u, 1u};
  const turbo_io_operation_kind pipe_read = TURBO_IO_PIPE_READ;
  const auto attach_pipe = &turbo_io_backend_attach_pipe;
  const auto release_pipe = &turbo_io_backend_release_pipe;
  static_assert(std::is_standard_layout_v<turbo_io_endpoint>);
  static_assert(sizeof(turbo_io_endpoint) == sizeof(std::uint32_t) * 2u);
  static_assert(offsetof(turbo_io_endpoint, slot) == 0u);
  static_assert(offsetof(turbo_io_endpoint, generation) == sizeof(std::uint32_t));
  return turbo_io_endpoint_valid(endpoint) && turbo_io_request_valid(request) &&
                 pipe_read == TURBO_IO_PIPE_READ && TURBO_IO_PIPE_WRITE == 6 &&
                 attach_pipe != nullptr && release_pipe != nullptr &&
                 turbo_io_backend_model(TURBO_IO_BACKEND_IOCP) == TURBO_IO_MODEL_COMPLETION &&
                 turbo_io_backend_model(TURBO_IO_BACKEND_EPOLL) == TURBO_IO_MODEL_READINESS &&
                 turbo_io_backend_model(TURBO_IO_BACKEND_IO_URING) == TURBO_IO_MODEL_COMPLETION &&
                 turbo_io_backend_model(TURBO_IO_BACKEND_KQUEUE) == TURBO_IO_MODEL_READINESS
             ? 0
             : 1;
}
