#include "cnet_transport.h"

#include <salts/error_codes.h>

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
  #include <arpa/inet.h>
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
  return error > 0 ? -error : SALTS_EIO;
}

static int cnet_transport_parse_address(const char *host, uint16_t port, bool allow_zero_port,
                                        void *out_address, size_t address_capacity,
                                        size_t *out_address_length) {
  struct sockaddr_in address_v4;
  struct sockaddr_in6 address_v6;
  int parsed;

  if (out_address_length == NULL) return SALTS_EINVAL;
  *out_address_length = 0u;
  if (host == NULL || host[0] == '\0' || (!allow_zero_port && port == 0u) || out_address == NULL)
    return SALTS_EINVAL;

  memset(&address_v4, 0, sizeof(address_v4));
  parsed = inet_pton(AF_INET, host, &address_v4.sin_addr);
  if (parsed < 0) return cnet_transport_native_error();
  if (parsed == 1) {
    if (address_capacity < sizeof(address_v4)) return SALTS_ERANGE;
    address_v4.sin_family = AF_INET;
    address_v4.sin_port = htons(port);
    memcpy(out_address, &address_v4, sizeof(address_v4));
    *out_address_length = sizeof(address_v4);
    return SALTS_OK;
  }

  memset(&address_v6, 0, sizeof(address_v6));
  parsed = inet_pton(AF_INET6, host, &address_v6.sin6_addr);
  if (parsed < 0) return cnet_transport_native_error();
  if (parsed == 1) {
    if (address_capacity < sizeof(address_v6)) return SALTS_ERANGE;
    address_v6.sin6_family = AF_INET6;
    address_v6.sin6_port = htons(port);
    memcpy(out_address, &address_v6, sizeof(address_v6));
    *out_address_length = sizeof(address_v6);
    return SALTS_OK;
  }
  return SALTS_ENOENT;
}

int cnet_transport_parse_numeric_address(const char *host, uint16_t port, void *out_address,
                                         size_t address_capacity, size_t *out_address_length) {
  return cnet_transport_parse_address(host, port, false, out_address, address_capacity,
                                      out_address_length);
}

int cnet_transport_parse_bind_address(const char *host, uint16_t port, void *out_address,
                                      size_t address_capacity, size_t *out_address_length) {
  return cnet_transport_parse_address(host, port, true, out_address, address_capacity,
                                      out_address_length);
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

void cnet_transport_close_socket(uintptr_t native_socket) {
  if (native_socket == UINTPTR_MAX) return;
#if defined(_WIN32)
  (void)closesocket((SOCKET)native_socket);
#else
  (void)close((int)native_socket);
#endif
}

static int cnet_transport_address_family(const void *address, size_t address_length,
                                         int *out_family) {
  const struct sockaddr *native_address = (const struct sockaddr *)address;
  if (address == NULL || out_family == NULL || address_length < sizeof(native_address->sa_family) ||
      address_length > (size_t)INT_MAX)
    return SALTS_EINVAL;
  if (native_address->sa_family == AF_INET) {
    if (address_length < sizeof(struct sockaddr_in)) return SALTS_EINVAL;
  } else if (native_address->sa_family == AF_INET6) {
    if (address_length < sizeof(struct sockaddr_in6)) return SALTS_EINVAL;
  } else {
    return SALTS_EINVAL;
  }
  *out_family = native_address->sa_family;
  return SALTS_OK;
}

static int cnet_transport_make_socket(native_io_backend_kind backend_kind, int family,
                                      int socket_type, int protocol,
                                      cnet_native_socket *out_socket) {
#if defined(_WIN32)
  if (backend_kind != NATIVE_IO_BACKEND_IOCP) return SALTS_ENOTSUP;
  *out_socket = WSASocketW(family, socket_type, protocol, NULL, 0u, WSA_FLAG_OVERLAPPED);
  return *out_socket == CNET_INVALID_SOCKET ? cnet_transport_native_error() : SALTS_OK;
#else
  int flags;
  *out_socket = socket(family, socket_type, protocol);
  if (*out_socket == CNET_INVALID_SOCKET) return cnet_transport_native_error();
  if (native_io_backend_kind_model(backend_kind) != NATIVE_IO_MODEL_READINESS) return SALTS_OK;
  flags = fcntl(*out_socket, F_GETFL, 0);
  if (flags >= 0 && fcntl(*out_socket, F_SETFL, flags | O_NONBLOCK) == 0) return SALTS_OK;
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

  if (transport == NULL || out_operation == NULL) return SALTS_EINVAL;
  cnet_transport_reset(transport);
  *out_operation = (native_io_operation){0};
  if (backend == NULL) return SALTS_EINVAL;
  status = cnet_transport_address_family(address, address_length, &family);
  if (status != SALTS_OK) return status;
  if (!native_io_backend_kind_supported(backend_kind)) return SALTS_ENOTSUP;
  status =
      cnet_transport_make_socket(backend_kind, family, SOCK_STREAM, IPPROTO_TCP, &socket_value);
  if (status != SALTS_OK) return status;

  transport->native_handle = (uintptr_t)socket_value;
  transport->resource_kind = CNET_TRANSPORT_RESOURCE_SOCKET;
  transport->native_open = true;
  status = native_io_backend_attach_socket(backend, transport->native_handle, &transport->endpoint);
  if (status != SALTS_OK) {
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
  return SALTS_OK;
}

int cnet_transport_tcp_connect(cnet_transport *transport, native_io_backend *backend,
                               native_io_backend_kind backend_kind, const void *address,
                               size_t address_length, uintptr_t user_data,
                               native_io_request *out_request) {
  native_io_operation operation;
  int status;

  if (out_request == NULL) return SALTS_EINVAL;
  *out_request = (native_io_request){0};
  status = cnet_transport_tcp_prepare_connect(transport, backend, backend_kind, address,
                                              address_length, user_data, &operation);
  if (status != SALTS_OK) return status;
  status = native_io_backend_submit(backend, &operation, out_request);
  if (status != SALTS_OK) {
    cnet_transport_close_native(transport);
    (void)native_io_backend_release_socket(backend, transport->endpoint);
    cnet_transport_reset(transport);
  }
  return status;
}

int cnet_transport_adopt_tcp(cnet_transport *transport, native_io_backend *backend,
                             uintptr_t native_socket) {
  int status;
  if (transport == NULL) return SALTS_EINVAL;
  cnet_transport_reset(transport);
  if (backend == NULL || native_socket == UINTPTR_MAX) return SALTS_EINVAL;

  transport->native_handle = native_socket;
  transport->resource_kind = CNET_TRANSPORT_RESOURCE_SOCKET;
  transport->native_open = true;
  status = native_io_backend_attach_socket(backend, native_socket, &transport->endpoint);
  if (status != SALTS_OK) {
    cnet_transport_close_native(transport);
    cnet_transport_reset(transport);
    return status;
  }
  transport->attached = true;
  return SALTS_OK;
}

int cnet_transport_udp_connect(cnet_transport *transport, native_io_backend *backend,
                               native_io_backend_kind backend_kind, const void *address,
                               size_t address_length) {
  cnet_native_socket socket_value = CNET_INVALID_SOCKET;
  int family = 0;
  int status;

  if (transport == NULL) return SALTS_EINVAL;
  cnet_transport_reset(transport);
  if (backend == NULL) return SALTS_EINVAL;
  status = cnet_transport_address_family(address, address_length, &family);
  if (status != SALTS_OK) return status;
  if (!native_io_backend_kind_supported(backend_kind)) return SALTS_ENOTSUP;
  status = cnet_transport_make_socket(backend_kind, family, SOCK_DGRAM, IPPROTO_UDP, &socket_value);
  if (status != SALTS_OK) return status;

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
  if (status != SALTS_OK) {
    cnet_transport_close_native(transport);
    cnet_transport_reset(transport);
    return status;
  }
  transport->attached = true;
  return SALTS_OK;
}

int cnet_transport_adopt_pipe(cnet_transport *transport, native_io_backend *backend,
                              uintptr_t read_handle, uintptr_t write_handle) {
  native_io_endpoint read_endpoint = {0};
  native_io_endpoint write_endpoint = {0};
  int status;
  if (transport == NULL) return SALTS_EINVAL;
  cnet_transport_reset(transport);
  if (backend == NULL || read_handle == UINTPTR_MAX || write_handle == UINTPTR_MAX)
    return SALTS_EINVAL;
  status = native_io_backend_attach_pipe(backend, read_handle,
                                         NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &read_endpoint);
  if (status != SALTS_OK) return status;
  if (write_handle != read_handle) {
    status = native_io_backend_attach_pipe(backend, write_handle,
                                           NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &write_endpoint);
    if (status != SALTS_OK) {
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
  return SALTS_OK;
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
    return SALTS_EINVAL;
  cnet_transport_close_native(transport);
  if (transport->attached) {
    status = transport->resource_kind == CNET_TRANSPORT_RESOURCE_PIPE
                 ? native_io_backend_release_pipe(backend, transport->endpoint)
                 : native_io_backend_release_socket(backend, transport->endpoint);
    if (status != SALTS_OK) return status;
    transport->attached = false;
  }
  if (transport->write_attached) {
    status = native_io_backend_release_pipe(backend, transport->write_endpoint);
    if (status != SALTS_OK) return status;
    transport->write_attached = false;
  }
  cnet_transport_reset(transport);
  return SALTS_OK;
}
