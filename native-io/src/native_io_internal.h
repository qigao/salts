#ifndef TURBO_NATIVE_IO_INTERNAL_H
#define TURBO_NATIVE_IO_INTERNAL_H

#include <turbo/native_io.h>

typedef struct turbo_io_impl turbo_io_impl;
typedef struct native_io_coroutine_owner native_io_coroutine_owner;

typedef enum turbo_io_resource_kind {
  TURBO_IO_RESOURCE_STREAM_SOCKET = 1,
  TURBO_IO_RESOURCE_DATAGRAM_SOCKET = 2,
  TURBO_IO_RESOURCE_BYTE_PIPE = 3
} turbo_io_resource_kind;

static inline bool native_io_resource_kind_is_socket(turbo_io_resource_kind kind) {
  return kind == TURBO_IO_RESOURCE_STREAM_SOCKET || kind == TURBO_IO_RESOURCE_DATAGRAM_SOCKET;
}

static inline turbo_io_resource_kind
native_io_operation_resource_kind(native_io_operation_kind kind) {
  if (kind == NATIVE_IO_OPERATION_TCP_RECV || kind == NATIVE_IO_OPERATION_TCP_SEND ||
      kind == NATIVE_IO_OPERATION_TCP_CONNECT)
    return TURBO_IO_RESOURCE_STREAM_SOCKET;
  if (kind == NATIVE_IO_OPERATION_UDP_RECV_FROM || kind == NATIVE_IO_OPERATION_UDP_SEND_TO)
    return TURBO_IO_RESOURCE_DATAGRAM_SOCKET;
  if (kind == NATIVE_IO_OPERATION_PIPE_READ || kind == NATIVE_IO_OPERATION_PIPE_WRITE)
    return TURBO_IO_RESOURCE_BYTE_PIPE;
  return (turbo_io_resource_kind)0;
}

typedef struct turbo_io_impl_ops {
  int (*attach_socket)(turbo_io_impl *impl, uintptr_t native_socket,
                       native_io_endpoint *out_endpoint);
  int (*release_socket)(turbo_io_impl *impl, native_io_endpoint endpoint);
  int (*submit)(turbo_io_impl *impl, const native_io_operation *operation,
                native_io_request *out_request);
  int (*cancel)(turbo_io_impl *impl, native_io_request request);
  int (*observe)(turbo_io_impl *impl, native_io_completion *events, size_t event_capacity,
                 uint32_t timeout_ms, size_t *out_count);
  int (*wake)(turbo_io_impl *impl);
  int (*close)(turbo_io_impl *impl);
  int (*destroy)(turbo_io_impl *impl);
  bool (*get_stats)(const turbo_io_impl *impl, native_io_backend_stats *out_stats);
  int (*attach_pipe)(turbo_io_impl *impl, uintptr_t native_handle, uint32_t flags,
                     native_io_endpoint *out_endpoint);
  int (*release_pipe)(turbo_io_impl *impl, native_io_endpoint endpoint);
} turbo_io_impl_ops;

struct turbo_io_impl {
  const turbo_io_impl_ops *ops;
  native_io_backend_kind kind;
  native_io_coroutine_owner *coroutine_owner;
  size_t coroutine_capacity;
  size_t coroutine_completion_capacity;
};

bool native_io_platform_backend_supported(native_io_backend_kind kind);
bool native_io_platform_pipe_supported(native_io_backend_kind kind);
int native_io_platform_backend_init(native_io_backend *backend,
                                    const native_io_backend_config *config);

#endif /* TURBO_NATIVE_IO_INTERNAL_H */
