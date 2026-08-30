#ifndef TURBO_NATIVE_IO_INTERNAL_H
#define TURBO_NATIVE_IO_INTERNAL_H

#include <turbo/native_io.h>

typedef struct turbo_io_impl turbo_io_impl;

typedef enum turbo_io_resource_kind {
  TURBO_IO_RESOURCE_SOCKET = 1,
  TURBO_IO_RESOURCE_BYTE_PIPE = 2
} turbo_io_resource_kind;

static inline turbo_io_resource_kind
turbo_io_operation_resource_kind(turbo_io_operation_kind kind) {
  if (kind == TURBO_IO_TCP_RECV || kind == TURBO_IO_TCP_SEND ||
      kind == TURBO_IO_UDP_RECV_FROM || kind == TURBO_IO_UDP_SEND_TO)
    return TURBO_IO_RESOURCE_SOCKET;
  if (kind == TURBO_IO_PIPE_READ || kind == TURBO_IO_PIPE_WRITE)
    return TURBO_IO_RESOURCE_BYTE_PIPE;
  return (turbo_io_resource_kind)0;
}

typedef struct turbo_io_impl_ops {
  int (*attach_socket)(turbo_io_impl *impl, uintptr_t native_socket,
                       turbo_io_endpoint *out_endpoint);
  int (*release_socket)(turbo_io_impl *impl, turbo_io_endpoint endpoint);
  int (*submit)(turbo_io_impl *impl, const turbo_io_operation *operation,
                turbo_io_request *out_request);
  int (*cancel)(turbo_io_impl *impl, turbo_io_request request);
  int (*observe)(turbo_io_impl *impl, turbo_io_completion *events, size_t event_capacity,
                 uint32_t timeout_ms, size_t *out_count);
  int (*close)(turbo_io_impl *impl);
  int (*destroy)(turbo_io_impl *impl);
  bool (*get_stats)(const turbo_io_impl *impl, turbo_io_backend_stats *out_stats);
  int (*attach_pipe)(turbo_io_impl *impl, uintptr_t native_handle, uint32_t flags,
                     turbo_io_endpoint *out_endpoint);
  int (*release_pipe)(turbo_io_impl *impl, turbo_io_endpoint endpoint);
} turbo_io_impl_ops;

struct turbo_io_impl {
  const turbo_io_impl_ops *ops;
  turbo_io_backend_kind kind;
};

bool turbo_io_platform_backend_supported(turbo_io_backend_kind kind);
bool turbo_io_platform_pipe_supported(turbo_io_backend_kind kind);
int turbo_io_platform_backend_init(turbo_io_backend *backend,
                                   const turbo_io_backend_config *config);

#endif /* TURBO_NATIVE_IO_INTERNAL_H */
