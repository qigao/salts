#include <turbo/native_io.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

int main() {
  const turbo_io_endpoint endpoint{1u, 1u};
  const turbo_io_request request{1u, 1u};
  const turbo_io_operation_kind pipe_read = TURBO_IO_PIPE_READ;
  const auto attach_pipe = &native_io_attach_pipe;
  const auto release_pipe = &native_io_release_pipe;
  static_assert(std::is_standard_layout_v<turbo_io_endpoint>);
  static_assert(sizeof(turbo_io_endpoint) == sizeof(std::uint32_t) * 2u);
  static_assert(offsetof(turbo_io_endpoint, slot) == 0u);
  static_assert(offsetof(turbo_io_endpoint, generation) == sizeof(std::uint32_t));
  (void)attach_pipe;
  (void)release_pipe;
  return turbo_io_endpoint_valid(endpoint) && turbo_io_request_valid(request) &&
                 pipe_read == TURBO_IO_PIPE_READ && TURBO_IO_PIPE_WRITE == 6 &&
                 native_io_get_model(TURBO_IO_BACKEND_IOCP) == TURBO_IO_MODEL_COMPLETION &&
                 native_io_get_model(TURBO_IO_BACKEND_EPOLL) == TURBO_IO_MODEL_READINESS &&
                 native_io_get_model(TURBO_IO_BACKEND_IO_URING) == TURBO_IO_MODEL_COMPLETION &&
                 native_io_get_model(TURBO_IO_BACKEND_KQUEUE) == TURBO_IO_MODEL_READINESS
             ? 0
             : 1;
}
