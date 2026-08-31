#include <turbo/native_io.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

int main() {
  const native_io_endpoint endpoint{1u, 1u};
  const native_io_request request{1u, 1u};
  const native_io_operation_kind pipe_read = NATIVE_IO_OPERATION_PIPE_READ;
  const auto attach_pipe = &native_io_backend_attach_pipe;
  const auto release_pipe = &native_io_backend_release_pipe;
  const auto wake_backend = &native_io_backend_wake;
  static_assert(std::is_standard_layout_v<native_io_endpoint>);
  static_assert(sizeof(native_io_endpoint) == sizeof(std::uint32_t) * 2u);
  static_assert(offsetof(native_io_endpoint, slot) == 0u);
  static_assert(offsetof(native_io_endpoint, generation) == sizeof(std::uint32_t));
  (void)attach_pipe;
  (void)release_pipe;
  (void)wake_backend;
  return native_io_endpoint_valid(endpoint) && native_io_request_valid(request) &&
                 pipe_read == NATIVE_IO_OPERATION_PIPE_READ &&
                 NATIVE_IO_OPERATION_PIPE_WRITE == 6 &&
                 native_io_backend_kind_model(NATIVE_IO_BACKEND_IOCP) ==
                     NATIVE_IO_MODEL_COMPLETION &&
                 native_io_backend_kind_model(NATIVE_IO_BACKEND_EPOLL) ==
                     NATIVE_IO_MODEL_READINESS &&
                 native_io_backend_kind_model(NATIVE_IO_BACKEND_IO_URING) ==
                     NATIVE_IO_MODEL_COMPLETION &&
                 native_io_backend_kind_model(NATIVE_IO_BACKEND_KQUEUE) ==
                     NATIVE_IO_MODEL_READINESS
             ? 0
             : 1;
}
