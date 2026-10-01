#include "cnet_transport.h"

#include <salts/error_codes.h>

#include <limits.h>
#include <string.h>

#if defined(_WIN32)
// clang-format off
  #include <winsock2.h>
  #include <mstcpip.h>
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
  #include <netinet/tcp.h>
  #if defined(__linux__)
    #include <linux/vm_sockets.h>
  #endif
  #include <sys/socket.h>
  #include <unistd.h>
typedef int cnet_native_socket;
  #define CNET_INVALID_SOCKET (-1)
#endif

static int cnet_transport_native_error(void);

int cnet_stream_socket_options_validate(const cnet_stream_socket_options *options) {
  if (options == NULL || options->size != sizeof(*options) ||
      (options->keepalive != 0 && options->keepalive != 1) ||
      (options->linger != 0 && options->linger != 1) ||
      (options->nodelay != 0 && options->nodelay != 1))
    return SALTS_EINVAL;
  if (options->receive_buffer_bytes > (size_t)INT_MAX ||
      options->send_buffer_bytes > (size_t)INT_MAX || options->keepalive_count > (uint32_t)INT_MAX)
    return SALTS_ERANGE;
  if (!options->keepalive && (options->keepalive_idle_ms != 0u ||
                              options->keepalive_interval_ms != 0u ||
                              options->keepalive_count != 0u))
    return SALTS_EINVAL;
  if (!options->linger && options->linger_ms != 0u) return SALTS_EINVAL;
  return SALTS_OK;
}

static int cnet_transport_set_socket_int(cnet_native_socket socket_value, int level, int option,
                                         int value) {
#if defined(_WIN32)
  if (setsockopt(socket_value, level, option, (const char *)&value, (int)sizeof(value)) != 0)
#else
  if (setsockopt(socket_value, level, option, &value, (socklen_t)sizeof(value)) != 0)
#endif
    return cnet_transport_native_error();
  return SALTS_OK;
}

static int cnet_transport_get_socket_int(cnet_native_socket socket_value, int level, int option,
                                         int *out_value) {
#if defined(_WIN32)
  int size = (int)sizeof(*out_value);
#else
  socklen_t size = (socklen_t)sizeof(*out_value);
#endif
  if (out_value == NULL) return SALTS_EINVAL;
  *out_value = 0;
#if defined(_WIN32)
  if (getsockopt(socket_value, level, option, (char *)out_value, &size) != 0)
#else
  if (getsockopt(socket_value, level, option, out_value, &size) != 0)
#endif
    return cnet_transport_native_error();
  return size == sizeof(*out_value) ? SALTS_OK : SALTS_EPROTO;
}

static int cnet_transport_socket_family(cnet_native_socket socket_value, int *out_family) {
  struct sockaddr_storage address;
#if defined(_WIN32)
  int size = (int)sizeof(address);
#else
  socklen_t size = (socklen_t)sizeof(address);
#endif
  if (out_family == NULL) return SALTS_EINVAL;
  *out_family = AF_UNSPEC;
  memset(&address, 0, sizeof(address));
  if (getsockname(socket_value, (struct sockaddr *)&address, &size) != 0)
    return cnet_transport_native_error();
  if (address.ss_family != AF_INET && address.ss_family != AF_INET6)
    return SALTS_EAFNOSUPPORT;
  *out_family = address.ss_family;
  return SALTS_OK;
}

static int cnet_transport_ms_to_seconds(uint64_t milliseconds, int *out_seconds) {
  uint64_t seconds;
  if (out_seconds == NULL || milliseconds == 0u) return SALTS_EINVAL;
  seconds = (milliseconds + UINT64_C(999)) / UINT64_C(1000);
  if (seconds == 0u || seconds > (uint64_t)INT_MAX) return SALTS_ERANGE;
  *out_seconds = (int)seconds;
  return SALTS_OK;
}

static int cnet_transport_duration_seconds(uint32_t milliseconds) {
  return (int)(((uint64_t)milliseconds + UINT64_C(999)) / UINT64_C(1000));
}

int cnet_transport_apply_stream_socket_options(
    uintptr_t native_socket, const cnet_stream_socket_options *options) {
  const cnet_native_socket socket_value = (cnet_native_socket)native_socket;
  int status;
  if (native_socket == UINTPTR_MAX) return SALTS_EINVAL;
  status = cnet_stream_socket_options_validate(options);
  if (status != SALTS_OK) return status;
#if defined(_WIN32)
  if (options->keepalive_count != 0u) return SALTS_ENOTSUP;
#else
  #if !defined(TCP_KEEPIDLE) && !defined(TCP_KEEPALIVE)
  if (options->keepalive_idle_ms != 0u) return SALTS_ENOTSUP;
  #endif
  #if !defined(TCP_KEEPINTVL)
  if (options->keepalive_interval_ms != 0u) return SALTS_ENOTSUP;
  #endif
  #if !defined(TCP_KEEPCNT)
  if (options->keepalive_count != 0u) return SALTS_ENOTSUP;
  #endif
#endif
  if (options->receive_buffer_bytes != 0u) {
    status = cnet_transport_set_socket_int(socket_value, SOL_SOCKET, SO_RCVBUF,
                                           (int)options->receive_buffer_bytes);
    if (status != SALTS_OK) return status;
  }
  if (options->send_buffer_bytes != 0u) {
    status = cnet_transport_set_socket_int(socket_value, SOL_SOCKET, SO_SNDBUF,
                                           (int)options->send_buffer_bytes);
    if (status != SALTS_OK) return status;
  }
  if (options->nodelay) {
    status = cnet_transport_set_socket_int(socket_value, IPPROTO_TCP, TCP_NODELAY, 1);
    if (status != SALTS_OK) return status;
  }
  if (options->keepalive) {
    status = cnet_transport_set_socket_int(socket_value, SOL_SOCKET, SO_KEEPALIVE, 1);
    if (status != SALTS_OK) return status;
#if defined(_WIN32)
    if (options->keepalive_idle_ms != 0u || options->keepalive_interval_ms != 0u) {
      struct tcp_keepalive keepalive = {
          1u, options->keepalive_idle_ms != 0u ? options->keepalive_idle_ms : 7200000u,
          options->keepalive_interval_ms != 0u ? options->keepalive_interval_ms : 1000u};
      DWORD bytes_returned = 0u;
      if (WSAIoctl(socket_value, SIO_KEEPALIVE_VALS, &keepalive, (DWORD)sizeof(keepalive), NULL, 0u,
                   &bytes_returned, NULL, NULL) != 0)
        return cnet_transport_native_error();
    }
#else
    if (options->keepalive_idle_ms != 0u) {
      #if defined(TCP_KEEPIDLE)
      status = cnet_transport_set_socket_int(
          socket_value, IPPROTO_TCP, TCP_KEEPIDLE,
          cnet_transport_duration_seconds(options->keepalive_idle_ms));
      #elif defined(TCP_KEEPALIVE)
      status = cnet_transport_set_socket_int(
          socket_value, IPPROTO_TCP, TCP_KEEPALIVE,
          cnet_transport_duration_seconds(options->keepalive_idle_ms));
      #endif
      if (status != SALTS_OK) return status;
    }
    if (options->keepalive_interval_ms != 0u) {
      #if defined(TCP_KEEPINTVL)
      status = cnet_transport_set_socket_int(
          socket_value, IPPROTO_TCP, TCP_KEEPINTVL,
          cnet_transport_duration_seconds(options->keepalive_interval_ms));
      #endif
      if (status != SALTS_OK) return status;
    }
    if (options->keepalive_count != 0u) {
      #if defined(TCP_KEEPCNT)
      status = cnet_transport_set_socket_int(socket_value, IPPROTO_TCP, TCP_KEEPCNT,
                                             (int)options->keepalive_count);
      #endif
      if (status != SALTS_OK) return status;
    }
#endif
  }
  if (options->linger) {
    struct linger linger_value = {1, cnet_transport_duration_seconds(options->linger_ms)};
#if defined(_WIN32)
    if (setsockopt(socket_value, SOL_SOCKET, SO_LINGER, (const char *)&linger_value,
                   (int)sizeof(linger_value)) != 0)
#else
    if (setsockopt(socket_value, SOL_SOCKET, SO_LINGER, &linger_value,
                   (socklen_t)sizeof(linger_value)) != 0)
#endif
      return cnet_transport_native_error();
  }
  return SALTS_OK;
}

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

int cnet_transport_stream_peer_address(const cnet_stream_peer *peer, bool allow_zero_port,
                                       void *out_address, size_t address_capacity,
                                       size_t *out_address_length) {
  if (out_address_length == NULL) return SALTS_EINVAL;
  *out_address_length = 0u;
  if (peer == NULL || out_address == NULL || (!allow_zero_port && peer->port == 0u))
    return SALTS_EINVAL;

  if (peer->family == CNET_DATAGRAM_ADDRESS_IPV4) {
    struct sockaddr_in address;
    if (address_capacity < sizeof(address)) return SALTS_ERANGE;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(peer->port);
    memcpy(&address.sin_addr, peer->address, 4u);
    memcpy(out_address, &address, sizeof(address));
    *out_address_length = sizeof(address);
    return SALTS_OK;
  }

  if (peer->family == CNET_DATAGRAM_ADDRESS_IPV6) {
    struct sockaddr_in6 address;
    if (address_capacity < sizeof(address)) return SALTS_ERANGE;
    memset(&address, 0, sizeof(address));
    address.sin6_family = AF_INET6;
    address.sin6_port = htons(peer->port);
    address.sin6_scope_id = peer->scope_id;
    memcpy(&address.sin6_addr, peer->address, 16u);
    memcpy(out_address, &address, sizeof(address));
    *out_address_length = sizeof(address);
    return SALTS_OK;
  }

  return SALTS_EAFNOSUPPORT;
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
#if defined(__linux__)
  } else if (native_address->sa_family == AF_VSOCK) {
    if (address_length < sizeof(struct sockaddr_vm)) return SALTS_EINVAL;
#endif
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

static bool cnet_transport_stream_socket_options_requested(
    const cnet_stream_socket_options *options) {
  return options->receive_buffer_bytes != 0u || options->send_buffer_bytes != 0u ||
         options->keepalive || options->linger;
}

static int cnet_transport_stream_prepare_connect_bound(
    cnet_transport *transport, native_io_backend *backend,
    native_io_backend_kind backend_kind, int family, int protocol,
    bool socket_options_supported, const void *address, size_t address_length,
    const void *local_address, size_t local_address_length,
    const cnet_stream_socket_options *socket_options, uintptr_t user_data,
    native_io_operation *out_operation) {
  cnet_native_socket socket_value = CNET_INVALID_SOCKET;
  int address_family = 0;
  int local_family = 0;
  int status;

  if (transport == NULL || out_operation == NULL) return SALTS_EINVAL;
  cnet_transport_reset(transport);
  *out_operation = (native_io_operation){0};
  if (backend == NULL) return SALTS_EINVAL;
  status = cnet_transport_address_family(address, address_length, &address_family);
  if (status != SALTS_OK) return status;
  if (address_family != family) return SALTS_EINVAL;
  if ((local_address == NULL) != (local_address_length == 0u)) return SALTS_EINVAL;
  if (local_address != NULL) {
    status = cnet_transport_address_family(local_address, local_address_length, &local_family);
    if (status != SALTS_OK) return status;
    if (local_family != family) return SALTS_EINVAL;
  }
  if (!native_io_backend_kind_supported(backend_kind)) return SALTS_ENOTSUP;
  status = cnet_stream_socket_options_validate(socket_options);
  if (status != SALTS_OK) return status;
  if (!socket_options_supported &&
      cnet_transport_stream_socket_options_requested(socket_options))
    return SALTS_ENOTSUP;
  status = cnet_transport_make_socket(backend_kind, family, SOCK_STREAM, protocol, &socket_value);
  if (status != SALTS_OK) return status;
  status = cnet_transport_apply_stream_socket_options((uintptr_t)socket_value, socket_options);
  if (status == SALTS_OK && local_address != NULL &&
      bind(socket_value, (const struct sockaddr *)local_address,
#if defined(_WIN32)
           (int)local_address_length
#else
           (socklen_t)local_address_length
#endif
           ) != 0)
    status = cnet_transport_native_error();
  if (status != SALTS_OK) {
#if defined(_WIN32)
    (void)closesocket(socket_value);
#else
    (void)close(socket_value);
#endif
    return status;
  }

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
  *out_operation = (native_io_operation){.kind = NATIVE_IO_OPERATION_STREAM_CONNECT,
                                         .endpoint = transport->endpoint,
                                         .user_data = user_data,
                                         .address = (void *)address,
                                         .address_capacity = address_length,
                                         .address_length = address_length};
  return SALTS_OK;
}

int cnet_transport_stream_prepare_connect(cnet_transport *transport, native_io_backend *backend,
                                          native_io_backend_kind backend_kind, int family,
                                          int protocol, bool socket_options_supported,
                                          const void *address, size_t address_length,
                                          const cnet_stream_socket_options *socket_options,
                                          uintptr_t user_data,
                                          native_io_operation *out_operation) {
  return cnet_transport_stream_prepare_connect_bound(
      transport, backend, backend_kind, family, protocol, socket_options_supported,
      address, address_length, NULL, 0u, socket_options, user_data, out_operation);
}

int cnet_transport_tcp_prepare_connect_bound(
    cnet_transport *transport, native_io_backend *backend,
    native_io_backend_kind backend_kind, const void *remote_address,
    size_t remote_address_length, const void *local_address,
    size_t local_address_length,
    const cnet_stream_socket_options *socket_options,
    uintptr_t user_data, native_io_operation *out_operation) {
  int family = 0;
  int status;
  if (transport == NULL || out_operation == NULL) return SALTS_EINVAL;
  cnet_transport_reset(transport);
  *out_operation = (native_io_operation){0};
  status = cnet_transport_address_family(remote_address, remote_address_length, &family);
  if (status != SALTS_OK) return status;
  if (family != AF_INET && family != AF_INET6) return SALTS_EINVAL;
  return cnet_transport_stream_prepare_connect_bound(
      transport, backend, backend_kind, family, IPPROTO_TCP, true,
      remote_address, remote_address_length, local_address, local_address_length,
      socket_options, user_data, out_operation);
}

int cnet_transport_tcp_prepare_connect(cnet_transport *transport, native_io_backend *backend,
                                       native_io_backend_kind backend_kind, const void *address,
                                       size_t address_length,
                                       const cnet_stream_socket_options *socket_options,
                                       uintptr_t user_data,
                                       native_io_operation *out_operation) {
  return cnet_transport_tcp_prepare_connect_bound(
      transport, backend, backend_kind, address, address_length, NULL, 0u,
      socket_options, user_data, out_operation);
}

int cnet_transport_tcp_connect(cnet_transport *transport, native_io_backend *backend,
                               native_io_backend_kind backend_kind, const void *address,
                               size_t address_length, uintptr_t user_data,
                               native_io_request *out_request) {
  native_io_operation operation;
  int status;

  if (out_request == NULL) return SALTS_EINVAL;
  *out_request = (native_io_request){0};
  {
    const cnet_stream_socket_options defaults = CNET_STREAM_SOCKET_OPTIONS_INIT;
    status = cnet_transport_tcp_prepare_connect(transport, backend, backend_kind, address,
                                                address_length, &defaults, user_data, &operation);
  }
  if (status != SALTS_OK) return status;
  status = native_io_backend_submit(backend, &operation, out_request);
  if (status != SALTS_OK) {
    cnet_transport_close_native(transport);
    (void)native_io_backend_release_socket(backend, transport->endpoint);
    cnet_transport_reset(transport);
  }
  return status;
}

int cnet_transport_adopt_stream(cnet_transport *transport, native_io_backend *backend,
                                uintptr_t native_socket, bool socket_options_supported,
                                const cnet_stream_socket_options *socket_options) {
  int status;
  if (transport == NULL) return SALTS_EINVAL;
  cnet_transport_reset(transport);
  if (backend == NULL || native_socket == UINTPTR_MAX) return SALTS_EINVAL;
  status = cnet_stream_socket_options_validate(socket_options);
  if (status != SALTS_OK) {
    cnet_transport_close_socket(native_socket);
    return status;
  }
  if (!socket_options_supported &&
      cnet_transport_stream_socket_options_requested(socket_options)) {
    cnet_transport_close_socket(native_socket);
    return SALTS_ENOTSUP;
  }
  status = cnet_transport_apply_stream_socket_options(native_socket, socket_options);
  if (status != SALTS_OK) {
    cnet_transport_close_socket(native_socket);
    return status;
  }

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

int cnet_transport_adopt_tcp(cnet_transport *transport, native_io_backend *backend,
                             uintptr_t native_socket,
                             const cnet_stream_socket_options *socket_options) {
  return cnet_transport_adopt_stream(transport, backend, native_socket, true, socket_options);
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

static int cnet_transport_socket_peer(const cnet_transport *transport, bool remote,
                                      cnet_stream_peer *out_peer) {
  struct sockaddr_storage address;
#if defined(_WIN32)
  int address_length = (int)sizeof(address);
#else
  socklen_t address_length = (socklen_t)sizeof(address);
#endif

  if (out_peer == NULL) return SALTS_EINVAL;
  *out_peer = (cnet_stream_peer){0};
  if (transport == NULL || transport->resource_kind != CNET_TRANSPORT_RESOURCE_SOCKET ||
      !transport->native_open || transport->native_handle == UINTPTR_MAX)
    return SALTS_ENOENT;

  memset(&address, 0, sizeof(address));
  if ((remote
           ? getpeername((cnet_native_socket)transport->native_handle,
                         (struct sockaddr *)&address, &address_length)
           : getsockname((cnet_native_socket)transport->native_handle,
                         (struct sockaddr *)&address, &address_length)) != 0)
    return cnet_transport_native_error();

  if (address.ss_family == AF_INET &&
      (size_t)address_length >= sizeof(struct sockaddr_in)) {
    const struct sockaddr_in *v4 = (const struct sockaddr_in *)&address;
    out_peer->family = CNET_DATAGRAM_ADDRESS_IPV4;
    out_peer->port = ntohs(v4->sin_port);
    memcpy(out_peer->address, &v4->sin_addr, 4u);
    return SALTS_OK;
  }
  if (address.ss_family == AF_INET6 &&
      (size_t)address_length >= sizeof(struct sockaddr_in6)) {
    const struct sockaddr_in6 *v6 = (const struct sockaddr_in6 *)&address;
    out_peer->family = CNET_DATAGRAM_ADDRESS_IPV6;
    out_peer->port = ntohs(v6->sin6_port);
    out_peer->scope_id = v6->sin6_scope_id;
    memcpy(out_peer->address, &v6->sin6_addr, 16u);
    return SALTS_OK;
  }
  return SALTS_EAFNOSUPPORT;
}

int cnet_transport_tcp_local_peer(const cnet_transport *transport, cnet_stream_peer *out_peer) {
  return cnet_transport_socket_peer(transport, false, out_peer);
}

int cnet_transport_tcp_remote_peer(const cnet_transport *transport, cnet_stream_peer *out_peer) {
  return cnet_transport_socket_peer(transport, true, out_peer);
}

static int cnet_transport_tcp_socket(const cnet_transport *transport,
                                     cnet_native_socket *out_socket) {
  if (out_socket == NULL) return SALTS_EINVAL;
  *out_socket = CNET_INVALID_SOCKET;
  if (transport == NULL ||
      transport->resource_kind != CNET_TRANSPORT_RESOURCE_SOCKET ||
      !transport->native_open || transport->native_handle == UINTPTR_MAX)
    return SALTS_ENOENT;
  *out_socket = (cnet_native_socket)transport->native_handle;
  return SALTS_OK;
}

int cnet_transport_tcp_option_get(const cnet_transport *transport,
                                  cnet_tcp_socket_option option,
                                  uint64_t *out_value) {
  cnet_native_socket socket_value;
  int family;
  int value;
  int status;

  if (out_value == NULL) return SALTS_EINVAL;
  *out_value = 0u;
  status = cnet_transport_tcp_socket(transport, &socket_value);
  if (status != SALTS_OK) return status;

  switch (option) {
  case CNET_TCP_SOCKET_KEEPALIVE_ENABLED:
    status = cnet_transport_get_socket_int(socket_value, SOL_SOCKET, SO_KEEPALIVE, &value);
    if (status == SALTS_OK) *out_value = value != 0 ? 1u : 0u;
    return status;

  case CNET_TCP_SOCKET_KEEPALIVE_IDLE_MS:
#if defined(TCP_KEEPIDLE)
    status = cnet_transport_get_socket_int(socket_value, IPPROTO_TCP, TCP_KEEPIDLE, &value);
#elif !defined(_WIN32) && defined(TCP_KEEPALIVE)
    status = cnet_transport_get_socket_int(socket_value, IPPROTO_TCP, TCP_KEEPALIVE, &value);
#else
    return SALTS_ENOTSUP;
#endif
    if (status == SALTS_OK) {
      if (value < 0) return SALTS_EPROTO;
      *out_value = (uint64_t)(unsigned int)value * UINT64_C(1000);
    }
    return status;

  case CNET_TCP_SOCKET_KEEPALIVE_INTERVAL_MS:
#if defined(TCP_KEEPINTVL)
    status = cnet_transport_get_socket_int(socket_value, IPPROTO_TCP, TCP_KEEPINTVL, &value);
    if (status == SALTS_OK) {
      if (value < 0) return SALTS_EPROTO;
      *out_value = (uint64_t)(unsigned int)value * UINT64_C(1000);
    }
    return status;
#else
    return SALTS_ENOTSUP;
#endif

  case CNET_TCP_SOCKET_KEEPALIVE_COUNT:
#if defined(TCP_KEEPCNT)
    status = cnet_transport_get_socket_int(socket_value, IPPROTO_TCP, TCP_KEEPCNT, &value);
    if (status == SALTS_OK) {
      if (value < 0) return SALTS_EPROTO;
      *out_value = (uint64_t)(unsigned int)value;
    }
    return status;
#else
    return SALTS_ENOTSUP;
#endif

  case CNET_TCP_SOCKET_HOP_LIMIT:
    status = cnet_transport_socket_family(socket_value, &family);
    if (status != SALTS_OK) return status;
    status = family == AF_INET
                 ? cnet_transport_get_socket_int(socket_value, IPPROTO_IP, IP_TTL, &value)
                 : cnet_transport_get_socket_int(socket_value, IPPROTO_IPV6,
                                                 IPV6_UNICAST_HOPS, &value);
    if (status == SALTS_OK) {
      if (value < 0) return SALTS_EPROTO;
      *out_value = (uint64_t)(unsigned int)value;
    }
    return status;

  case CNET_TCP_SOCKET_RECEIVE_BUFFER_BYTES:
    status = cnet_transport_get_socket_int(socket_value, SOL_SOCKET, SO_RCVBUF, &value);
    if (status == SALTS_OK) {
      if (value < 0) return SALTS_EPROTO;
      *out_value = (uint64_t)(unsigned int)value;
    }
    return status;

  case CNET_TCP_SOCKET_SEND_BUFFER_BYTES:
    status = cnet_transport_get_socket_int(socket_value, SOL_SOCKET, SO_SNDBUF, &value);
    if (status == SALTS_OK) {
      if (value < 0) return SALTS_EPROTO;
      *out_value = (uint64_t)(unsigned int)value;
    }
    return status;
  }

  return SALTS_EINVAL;
}

int cnet_transport_tcp_option_set(cnet_transport *transport,
                                  cnet_tcp_socket_option option,
                                  uint64_t option_value) {
  cnet_native_socket socket_value;
  int family;
  int value;
  int status;

  status = cnet_transport_tcp_socket(transport, &socket_value);
  if (status != SALTS_OK) return status;

  switch (option) {
  case CNET_TCP_SOCKET_KEEPALIVE_ENABLED:
    if (option_value > 1u) return SALTS_EINVAL;
    return cnet_transport_set_socket_int(
        socket_value, SOL_SOCKET, SO_KEEPALIVE, (int)option_value);

  case CNET_TCP_SOCKET_KEEPALIVE_IDLE_MS:
    status = cnet_transport_ms_to_seconds(option_value, &value);
    if (status != SALTS_OK) return status;
#if defined(TCP_KEEPIDLE)
    return cnet_transport_set_socket_int(socket_value, IPPROTO_TCP, TCP_KEEPIDLE, value);
#elif !defined(_WIN32) && defined(TCP_KEEPALIVE)
    return cnet_transport_set_socket_int(socket_value, IPPROTO_TCP, TCP_KEEPALIVE, value);
#else
    return SALTS_ENOTSUP;
#endif

  case CNET_TCP_SOCKET_KEEPALIVE_INTERVAL_MS:
    status = cnet_transport_ms_to_seconds(option_value, &value);
    if (status != SALTS_OK) return status;
#if defined(TCP_KEEPINTVL)
    return cnet_transport_set_socket_int(socket_value, IPPROTO_TCP, TCP_KEEPINTVL, value);
#else
    return SALTS_ENOTSUP;
#endif

  case CNET_TCP_SOCKET_KEEPALIVE_COUNT:
    if (option_value == 0u || option_value > (uint64_t)INT_MAX) return SALTS_ERANGE;
#if defined(TCP_KEEPCNT)
    return cnet_transport_set_socket_int(
        socket_value, IPPROTO_TCP, TCP_KEEPCNT, (int)option_value);
#else
    return SALTS_ENOTSUP;
#endif

  case CNET_TCP_SOCKET_HOP_LIMIT:
    if (option_value == 0u || option_value > UINT8_MAX) return SALTS_ERANGE;
    status = cnet_transport_socket_family(socket_value, &family);
    if (status != SALTS_OK) return status;
    return family == AF_INET
               ? cnet_transport_set_socket_int(
                     socket_value, IPPROTO_IP, IP_TTL, (int)option_value)
               : cnet_transport_set_socket_int(
                     socket_value, IPPROTO_IPV6, IPV6_UNICAST_HOPS,
                     (int)option_value);

  case CNET_TCP_SOCKET_RECEIVE_BUFFER_BYTES:
    if (option_value == 0u || option_value > (uint64_t)INT_MAX) return SALTS_ERANGE;
    return cnet_transport_set_socket_int(
        socket_value, SOL_SOCKET, SO_RCVBUF, (int)option_value);

  case CNET_TCP_SOCKET_SEND_BUFFER_BYTES:
    if (option_value == 0u || option_value > (uint64_t)INT_MAX) return SALTS_ERANGE;
    return cnet_transport_set_socket_int(
        socket_value, SOL_SOCKET, SO_SNDBUF, (int)option_value);
  }

  return SALTS_EINVAL;
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
