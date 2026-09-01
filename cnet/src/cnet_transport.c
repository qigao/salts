#include "cnet_transport.h"

#include <turbo/error_codes.h>

#include <limits.h>
#include <string.h>

#if defined(_WIN32)
// clang-format off
  #include <winsock2.h>
  #include <windows.h>
  #include <ws2tcpip.h>
// clang-format on
typedef SOCKET cnet_native_socket;
  #define CNET_INVALID_SOCKET INVALID_SOCKET
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
typedef int cnet_native_socket;
  #define CNET_INVALID_SOCKET (-1)
#endif

static void cnet_transport_reset(cnet_transport *transport) {
  if (transport == NULL) return;
  *transport = (cnet_transport){.native_handle = UINTPTR_MAX,
                                .write_native_handle = UINTPTR_MAX,
                                .resource_kind = CNET_TRANSPORT_RESOURCE_NONE};
}

static int cnet_transport_native_error(void) {
#if defined(_WIN32)
  const int error = WSAGetLastError();
#else
  const int error = errno;
#endif
  return error > 0 ? -error : TURBO_EIO;
}

static void cnet_transport_close_native(cnet_transport *transport) {
  if (transport == NULL) return;
  if (transport->native_open) {
#if defined(_WIN32)
    if (transport->resource_kind == CNET_TRANSPORT_RESOURCE_SOCKET)
      (void)closesocket((SOCKET)transport->native_handle);
    else (void)CloseHandle((HANDLE)transport->native_handle);
#else
    (void)close((int)transport->native_handle);
#endif
    transport->native_open = false;
  }
  if (transport->write_native_open) {
#if defined(_WIN32)
    (void)CloseHandle((HANDLE)transport->write_native_handle);
#else
    (void)close((int)transport->write_native_handle);
#endif
    transport->write_native_open = false;
  }
}

static int cnet_transport_address_family(const void *address, size_t address_length,
                                         int *out_family) {
  const struct sockaddr *native_address = (const struct sockaddr *)address;
  if (address == NULL || out_family == NULL || address_length < sizeof(native_address->sa_family) ||
      address_length > (size_t)INT_MAX)
    return TURBO_EINVAL;
  if (native_address->sa_family == AF_INET) {
    if (address_length < sizeof(struct sockaddr_in)) return TURBO_EINVAL;
  } else if (native_address->sa_family == AF_INET6) {
    if (address_length < sizeof(struct sockaddr_in6)) return TURBO_EINVAL;
  } else {
    return TURBO_EINVAL;
  }
  *out_family = native_address->sa_family;
  return TURBO_OK;
}

static int cnet_transport_make_socket(native_io_backend_kind backend_kind, int family,
                                      int socket_type, int protocol,
                                      cnet_native_socket *out_socket) {
#if defined(_WIN32)
  if (backend_kind != NATIVE_IO_BACKEND_IOCP) return TURBO_ENOTSUP;
  *out_socket = WSASocketW(family, socket_type, protocol, NULL, 0u, WSA_FLAG_OVERLAPPED);
  return *out_socket == CNET_INVALID_SOCKET ? cnet_transport_native_error() : TURBO_OK;
#else
  int flags;
  *out_socket = socket(family, socket_type, protocol);
  if (*out_socket == CNET_INVALID_SOCKET) return cnet_transport_native_error();
  if (native_io_backend_kind_model(backend_kind) != NATIVE_IO_MODEL_READINESS) return TURBO_OK;
  flags = fcntl(*out_socket, F_GETFL, 0);
  if (flags >= 0 && fcntl(*out_socket, F_SETFL, flags | O_NONBLOCK) == 0) return TURBO_OK;
  {
    const int status = cnet_transport_native_error();
    (void)close(*out_socket);
    *out_socket = CNET_INVALID_SOCKET;
    return status;
  }
#endif
}

int cnet_transport_tcp_prepare_connect(cnet_transport *transport, native_io_backend *backend,
                                       native_io_backend_kind backend_kind, const void *address,
                                       size_t address_length, uintptr_t user_data,
                                       native_io_operation *out_operation) {
  cnet_native_socket socket_value = CNET_INVALID_SOCKET;
  int family = 0;
  int status;

  if (transport == NULL || out_operation == NULL) return TURBO_EINVAL;
  cnet_transport_reset(transport);
  *out_operation = (native_io_operation){0};
  if (backend == NULL) return TURBO_EINVAL;
  status = cnet_transport_address_family(address, address_length, &family);
  if (status != TURBO_OK) return status;
  if (!native_io_backend_kind_supported(backend_kind)) return TURBO_ENOTSUP;
  status =
      cnet_transport_make_socket(backend_kind, family, SOCK_STREAM, IPPROTO_TCP, &socket_value);
  if (status != TURBO_OK) return status;

  transport->native_handle = (uintptr_t)socket_value;
  transport->resource_kind = CNET_TRANSPORT_RESOURCE_SOCKET;
  transport->native_open = true;
  status = native_io_backend_attach_socket(backend, transport->native_handle, &transport->endpoint);
  if (status != TURBO_OK) {
    cnet_transport_close_native(transport);
    cnet_transport_reset(transport);
    return status;
  }
  transport->attached = true;
  *out_operation = (native_io_operation){.kind = NATIVE_IO_OPERATION_TCP_CONNECT,
                                         .endpoint = transport->endpoint,
                                         .user_data = user_data,
                                         .address = (void *)address,
                                         .address_capacity = address_length,
                                         .address_length = address_length};
  return TURBO_OK;
}

int cnet_transport_tcp_connect(cnet_transport *transport, native_io_backend *backend,
                               native_io_backend_kind backend_kind, const void *address,
                               size_t address_length, uintptr_t user_data,
                               native_io_request *out_request) {
  native_io_operation operation;
  int status;

  if (out_request == NULL) return TURBO_EINVAL;
  *out_request = (native_io_request){0};
  status = cnet_transport_tcp_prepare_connect(transport, backend, backend_kind, address,
                                              address_length, user_data, &operation);
  if (status != TURBO_OK) return status;
  status = native_io_backend_submit(backend, &operation, out_request);
  if (status != TURBO_OK) {
    cnet_transport_close_native(transport);
    (void)native_io_backend_release_socket(backend, transport->endpoint);
    cnet_transport_reset(transport);
  }
  return status;
}

int cnet_transport_udp_connect(cnet_transport *transport, native_io_backend *backend,
                               native_io_backend_kind backend_kind, const void *address,
                               size_t address_length) {
  cnet_native_socket socket_value = CNET_INVALID_SOCKET;
  int family = 0;
  int status;

  if (transport == NULL) return TURBO_EINVAL;
  cnet_transport_reset(transport);
  if (backend == NULL) return TURBO_EINVAL;
  status = cnet_transport_address_family(address, address_length, &family);
  if (status != TURBO_OK) return status;
  if (!native_io_backend_kind_supported(backend_kind)) return TURBO_ENOTSUP;
  status = cnet_transport_make_socket(backend_kind, family, SOCK_DGRAM, IPPROTO_UDP, &socket_value);
  if (status != TURBO_OK) return status;

  transport->native_handle = (uintptr_t)socket_value;
  transport->resource_kind = CNET_TRANSPORT_RESOURCE_SOCKET;
  transport->native_open = true;
  if (connect(socket_value, (const struct sockaddr *)address, (int)address_length) != 0) {
    status = cnet_transport_native_error();
    cnet_transport_close_native(transport);
    cnet_transport_reset(transport);
    return status;
  }
  status = native_io_backend_attach_socket(backend, transport->native_handle, &transport->endpoint);
  if (status != TURBO_OK) {
    cnet_transport_close_native(transport);
    cnet_transport_reset(transport);
    return status;
  }
  transport->attached = true;
  return TURBO_OK;
}

int cnet_transport_adopt_pipe(cnet_transport *transport, native_io_backend *backend,
                              uintptr_t read_handle, uintptr_t write_handle) {
  native_io_endpoint read_endpoint = {0};
  native_io_endpoint write_endpoint = {0};
  int status;
  if (transport == NULL) return TURBO_EINVAL;
  cnet_transport_reset(transport);
  if (backend == NULL || read_handle == UINTPTR_MAX || write_handle == UINTPTR_MAX)
    return TURBO_EINVAL;
  status = native_io_backend_attach_pipe(backend, read_handle,
                                         NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &read_endpoint);
  if (status != TURBO_OK) return status;
  if (write_handle != read_handle) {
    status = native_io_backend_attach_pipe(backend, write_handle,
                                           NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &write_endpoint);
    if (status != TURBO_OK) {
      (void)native_io_backend_release_pipe(backend, read_endpoint);
      return status;
    }
  } else {
    write_endpoint = read_endpoint;
  }
  transport->native_handle = read_handle;
  transport->endpoint = read_endpoint;
  transport->write_native_handle = write_handle != read_handle ? write_handle : UINTPTR_MAX;
  transport->write_endpoint = write_endpoint;
  transport->resource_kind = CNET_TRANSPORT_RESOURCE_PIPE;
  transport->native_open = true;
  transport->attached = true;
  transport->write_native_open = write_handle != read_handle;
  transport->write_attached = write_handle != read_handle;
  return TURBO_OK;
}

native_io_endpoint cnet_transport_read_endpoint(const cnet_transport *transport) {
  return transport != NULL && transport->attached ? transport->endpoint : (native_io_endpoint){0};
}

native_io_endpoint cnet_transport_write_endpoint(const cnet_transport *transport) {
  if (transport == NULL || !transport->attached) return (native_io_endpoint){0};
  return transport->write_attached ? transport->write_endpoint : transport->endpoint;
}

bool cnet_transport_active(const cnet_transport *transport) {
  return transport != NULL && (transport->native_open || transport->attached ||
                               transport->write_native_open || transport->write_attached);
}

int cnet_transport_close(cnet_transport *transport, native_io_backend *backend) {
  int status;
  if (transport == NULL || backend == NULL || !cnet_transport_active(transport))
    return TURBO_EINVAL;
  cnet_transport_close_native(transport);
  if (transport->attached) {
    status = transport->resource_kind == CNET_TRANSPORT_RESOURCE_PIPE
                 ? native_io_backend_release_pipe(backend, transport->endpoint)
                 : native_io_backend_release_socket(backend, transport->endpoint);
    if (status != TURBO_OK) return status;
    transport->attached = false;
  }
  if (transport->write_attached) {
    status = native_io_backend_release_pipe(backend, transport->write_endpoint);
    if (status != TURBO_OK) return status;
    transport->write_attached = false;
  }
  cnet_transport_reset(transport);
  return TURBO_OK;
}
