#include "cnet_transport.h"

#include <turbo/error_codes.h>

#include <limits.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
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
  *transport = (cnet_transport){UINTPTR_MAX, {0u, 0u}, false, false};
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
  if (transport == NULL || !transport->native_open) return;
#if defined(_WIN32)
  (void)closesocket((SOCKET)transport->native_handle);
#else
  (void)close((int)transport->native_handle);
#endif
  transport->native_open = false;
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

int cnet_transport_tcp_connect(cnet_transport *transport, native_io_backend *backend,
                               native_io_backend_kind backend_kind, const void *address,
                               size_t address_length, uintptr_t user_data,
                               native_io_request *out_request) {
  cnet_native_socket socket_value = CNET_INVALID_SOCKET;
  native_io_operation operation;
  int family = 0;
  int status;

  if (transport == NULL || out_request == NULL) return TURBO_EINVAL;
  cnet_transport_reset(transport);
  *out_request = (native_io_request){0};
  if (backend == NULL) return TURBO_EINVAL;
  status = cnet_transport_address_family(address, address_length, &family);
  if (status != TURBO_OK) return status;
  if (!native_io_backend_kind_supported(backend_kind)) return TURBO_ENOTSUP;
  status =
      cnet_transport_make_socket(backend_kind, family, SOCK_STREAM, IPPROTO_TCP, &socket_value);
  if (status != TURBO_OK) return status;

  transport->native_handle = (uintptr_t)socket_value;
  transport->native_open = true;
  status = native_io_backend_attach_socket(backend, transport->native_handle, &transport->endpoint);
  if (status != TURBO_OK) {
    cnet_transport_close_native(transport);
    cnet_transport_reset(transport);
    return status;
  }
  transport->attached = true;
  operation = (native_io_operation){.kind = NATIVE_IO_OPERATION_TCP_CONNECT,
                                    .endpoint = transport->endpoint,
                                    .user_data = user_data,
                                    .address = (void *)address,
                                    .address_capacity = address_length,
                                    .address_length = address_length};
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

int cnet_transport_close(cnet_transport *transport, native_io_backend *backend) {
  int status;
  if (transport == NULL || backend == NULL || (!transport->native_open && !transport->attached))
    return TURBO_EINVAL;
  cnet_transport_close_native(transport);
  if (!transport->attached) {
    cnet_transport_reset(transport);
    return TURBO_OK;
  }
  status = native_io_backend_release_socket(backend, transport->endpoint);
  if (status != TURBO_OK) return status;
  cnet_transport_reset(transport);
  return TURBO_OK;
}
