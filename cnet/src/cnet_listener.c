#include <cnet/cnet.h>

#include "cnet_client_internal.h"
#include "cnet_module.h"
#include "cnet_transport.h"

#include <salts/clock.h>

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
// clang-format off
  #include <winsock2.h>
  #include <windows.h>
  #include <ws2tcpip.h>
// clang-format on
typedef SOCKET cnet_listener_socket;
  #define CNET_LISTENER_INVALID_SOCKET INVALID_SOCKET
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <poll.h>
  #include <sys/socket.h>
  #include <unistd.h>
typedef int cnet_listener_socket;
  #define CNET_LISTENER_INVALID_SOCKET (-1)
#endif

enum { CNET_LISTENER_ADDRESS_CAPACITY = 128 };

typedef struct cnet_listener_impl {
  cnet_listener_socket socket_value;
  native_io_backend_kind backend;
  uint16_t port;
  bool closed;
} cnet_listener_impl;

static cnet_listener_impl *cnet_listener_get(cnet_listener *listener) {
  return listener != NULL ? (cnet_listener_impl *)listener->impl : NULL;
}

static const cnet_listener_impl *cnet_listener_const_get(const cnet_listener *listener) {
  return listener != NULL ? (const cnet_listener_impl *)listener->impl : NULL;
}

static int cnet_listener_native_status(int error) {
#if defined(_WIN32)
  if (error == WSAEADDRINUSE) return SALTS_EADDRINUSE;
  if (error == WSAEADDRNOTAVAIL) return SALTS_EADDRNOTAVAIL;
  if (error == WSAEAFNOSUPPORT) return SALTS_EAFNOSUPPORT;
  if (error == WSAEALREADY) return SALTS_EALREADY;
  if (error == WSAEBADF) return SALTS_EBADF;
  if (error == WSAEACCES) return SALTS_EPERM;
  if (error == WSAECONNABORTED) return SALTS_ECONNABORTED;
  if (error == WSAECONNREFUSED) return SALTS_ECONNREFUSED;
  if (error == WSAECONNRESET) return SALTS_ECONNRESET;
  if (error == WSAEDESTADDRREQ) return SALTS_EDESTADDRREQ;
  if (error == WSAEFAULT) return SALTS_EFAULT;
  if (error == WSAEHOSTUNREACH) return SALTS_EHOSTUNREACH;
  if (error == WSAEINTR) return SALTS_EINTR;
  if (error == WSAEINVAL) return SALTS_EINVAL;
  if (error == WSAEISCONN) return SALTS_EISCONN;
  if (error == WSAEMFILE) return SALTS_EMFILE;
  if (error == WSAEMSGSIZE) return SALTS_EMSGSIZE;
  if (error == WSAENETDOWN) return SALTS_ENETDOWN;
  if (error == WSAENETUNREACH) return SALTS_ENETUNREACH;
  if (error == WSAENOBUFS) return SALTS_ENOBUFS;
  if (error == WSAENOPROTOOPT) return SALTS_ENOPROTOOPT;
  if (error == WSAENOTCONN) return SALTS_ENOTCONN;
  if (error == WSAENOTSOCK) return SALTS_ENOTSOCK;
  if (error == WSAEOPNOTSUPP) return SALTS_ENOTSUP;
  if (error == WSAEPROTONOSUPPORT) return SALTS_EPROTONOSUPPORT;
  if (error == WSAEPROTOTYPE) return SALTS_EPROTOTYPE;
  if (error == WSAESHUTDOWN) return SALTS_ESHUTDOWN;
  if (error == WSAETIMEDOUT) return SALTS_ETIMEDOUT;
  if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS) return SALTS_EBUSY;
#else
  if (error == EADDRINUSE) return SALTS_EADDRINUSE;
  if (error == EADDRNOTAVAIL) return SALTS_EADDRNOTAVAIL;
  if (error == EAFNOSUPPORT) return SALTS_EAFNOSUPPORT;
  if (error == EALREADY) return SALTS_EALREADY;
  if (error == EBADF) return SALTS_EBADF;
  if (error == EBUSY) return SALTS_EBUSY;
  if (error == EACCES || error == EPERM) return SALTS_EPERM;
  if (error == ECONNABORTED) return SALTS_ECONNABORTED;
  if (error == ECONNREFUSED) return SALTS_ECONNREFUSED;
  if (error == ECONNRESET) return SALTS_ECONNRESET;
  if (error == EDESTADDRREQ) return SALTS_EDESTADDRREQ;
  if (error == EFAULT) return SALTS_EFAULT;
  if (error == EHOSTUNREACH) return SALTS_EHOSTUNREACH;
  if (error == EINTR) return SALTS_EINTR;
  if (error == EINVAL) return SALTS_EINVAL;
  if (error == EISCONN) return SALTS_EISCONN;
  if (error == EMFILE) return SALTS_EMFILE;
  if (error == EMSGSIZE) return SALTS_EMSGSIZE;
  if (error == ENETDOWN) return SALTS_ENETDOWN;
  if (error == ENETUNREACH) return SALTS_ENETUNREACH;
  if (error == ENFILE) return SALTS_ENFILE;
  if (error == ENOBUFS) return SALTS_ENOBUFS;
  if (error == ENOMEM) return SALTS_ENOMEM;
  if (error == ENOPROTOOPT) return SALTS_ENOPROTOOPT;
  if (error == ENOTCONN) return SALTS_ENOTCONN;
  if (error == ENOTSOCK) return SALTS_ENOTSOCK;
  if (error == EOPNOTSUPP) return SALTS_ENOTSUP;
  if (error == EPROTONOSUPPORT) return SALTS_EPROTONOSUPPORT;
  if (error == EPROTOTYPE) return SALTS_EPROTOTYPE;
  if (error == EPIPE) return SALTS_EPIPE;
  if (error == ETIMEDOUT) return SALTS_ETIMEDOUT;
  if (error == EAGAIN || error == EWOULDBLOCK || error == EINPROGRESS) return SALTS_EBUSY;
#endif
  return SALTS_EIO;
}

static int cnet_listener_native_error(void) {
#if defined(_WIN32)
  return cnet_listener_native_status(WSAGetLastError());
#else
  return cnet_listener_native_status(errno);
#endif
}

static bool cnet_listener_would_block(void) {
#if defined(_WIN32)
  return WSAGetLastError() == WSAEWOULDBLOCK;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static int cnet_listener_stream_peer(const struct sockaddr_storage *native_peer,
                                     size_t native_size, cnet_stream_peer *peer) {
  if (native_peer == NULL || peer == NULL) return SALTS_EINVAL;
  *peer = (cnet_stream_peer){0};
  if (native_peer->ss_family == AF_INET && native_size >= sizeof(struct sockaddr_in)) {
    const struct sockaddr_in *address = (const struct sockaddr_in *)native_peer;
    peer->family = CNET_DATAGRAM_ADDRESS_IPV4;
    peer->port = ntohs(address->sin_port);
    memcpy(peer->address, &address->sin_addr, 4u);
    return SALTS_OK;
  }
  if (native_peer->ss_family == AF_INET6 && native_size >= sizeof(struct sockaddr_in6)) {
    const struct sockaddr_in6 *address = (const struct sockaddr_in6 *)native_peer;
    peer->family = CNET_DATAGRAM_ADDRESS_IPV6;
    peer->port = ntohs(address->sin6_port);
    peer->scope_id = address->sin6_scope_id;
    memcpy(peer->address, &address->sin6_addr, 16u);
    return SALTS_OK;
  }
  return SALTS_EAFNOSUPPORT;
}

static void cnet_listener_close_native(cnet_listener_impl *impl) {
  if (impl == NULL || impl->socket_value == CNET_LISTENER_INVALID_SOCKET) return;
#if defined(_WIN32)
  (void)closesocket(impl->socket_value);
#else
  (void)close(impl->socket_value);
#endif
  impl->socket_value = CNET_LISTENER_INVALID_SOCKET;
}

static int cnet_listener_set_nonblocking(cnet_listener_socket socket_value) {
#if defined(_WIN32)
  u_long enabled = 1u;
  if (ioctlsocket(socket_value, FIONBIO, &enabled) == 0) return SALTS_OK;
#else
  const int flags = fcntl(socket_value, F_GETFL, 0);
  if (flags >= 0 && fcntl(socket_value, F_SETFL, flags | O_NONBLOCK) == 0) return SALTS_OK;
#endif
  return cnet_listener_native_error();
}

static int cnet_listener_bound_port(cnet_listener_socket socket_value, uint16_t *out_port) {
  struct sockaddr_storage address;
#if defined(_WIN32)
  int address_length = (int)sizeof(address);
#else
  socklen_t address_length = (socklen_t)sizeof(address);
#endif
  memset(&address, 0, sizeof(address));
  if (getsockname(socket_value, (struct sockaddr *)&address, &address_length) != 0)
    return cnet_listener_native_error();
  if (address.ss_family == AF_INET)
    *out_port = ntohs(((const struct sockaddr_in *)&address)->sin_port);
  else if (address.ss_family == AF_INET6)
    *out_port = ntohs(((const struct sockaddr_in6 *)&address)->sin6_port);
  else return SALTS_EPROTO;
  return *out_port != 0u ? SALTS_OK : SALTS_EPROTO;
}

int cnet_listener_options_validate(const cnet_listener_options *options) {
  if (options == NULL || options->size != sizeof(*options) ||
      (options->reuse_port != 0 && options->reuse_port != 1))
    return SALTS_EINVAL;
  return SALTS_OK;
}

int cnet_listener_init_ex(cnet_listener *listener, const cnet_listener_config *config,
                          const cnet_listener_options *options) {
  cnet_listener_impl *impl;
  unsigned char address[CNET_LISTENER_ADDRESS_CAPACITY];
  size_t address_length = 0u;
  int family;
  int status;
#if !defined(_WIN32)
  const int reuse_address = 1;
#endif

  if (listener == NULL || config == NULL) return SALTS_EINVAL;
  if (listener->impl != NULL) return SALTS_EALREADY;
  status = cnet_listener_options_validate(options);
  if (status != SALTS_OK) return status;
#if !defined(SO_REUSEPORT)
  if (options->reuse_port) return SALTS_ENOTSUP;
#endif
  if (config->host == NULL || config->backlog == 0u || config->backlog > INT_MAX ||
      !native_io_backend_kind_supported(config->backend))
    return SALTS_EINVAL;
  status = cnet_transport_parse_bind_address(config->host, config->port, address, sizeof(address),
                                             &address_length);
  if (status != SALTS_OK) return status;
  family = ((const struct sockaddr *)address)->sa_family;

  status = cnet_module_init();
  if (status != SALTS_OK) return status;
  impl = (cnet_listener_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) {
    (void)cnet_module_shutdown();
    return SALTS_ENOMEM;
  }
  impl->socket_value = CNET_LISTENER_INVALID_SOCKET;
  impl->backend = config->backend;
#if defined(_WIN32)
  if (config->backend != NATIVE_IO_BACKEND_IOCP) status = SALTS_ENOTSUP;
  else {
    impl->socket_value =
        WSASocketW(family, SOCK_STREAM, IPPROTO_TCP, NULL, 0u, WSA_FLAG_OVERLAPPED);
    status = impl->socket_value != CNET_LISTENER_INVALID_SOCKET ? SALTS_OK
                                                                : cnet_listener_native_error();
  }
#else
  impl->socket_value = socket(family, SOCK_STREAM, IPPROTO_TCP);
  status =
      impl->socket_value != CNET_LISTENER_INVALID_SOCKET ? SALTS_OK : cnet_listener_native_error();
  if (status == SALTS_OK && setsockopt(impl->socket_value, SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                                       (socklen_t)sizeof(reuse_address)) != 0)
    status = cnet_listener_native_error();
#endif
#if defined(SO_REUSEPORT)
  if (status == SALTS_OK && options->reuse_port) {
    const int reuse_port = 1;
#if defined(_WIN32)
    if (setsockopt(impl->socket_value, SOL_SOCKET, SO_REUSEPORT, (const char *)&reuse_port,
                   (int)sizeof(reuse_port)) != 0)
#else
    if (setsockopt(impl->socket_value, SOL_SOCKET, SO_REUSEPORT, &reuse_port,
                   (socklen_t)sizeof(reuse_port)) != 0)
#endif
      status = cnet_listener_native_error();
  }
#endif
  if (status == SALTS_OK) status = cnet_listener_set_nonblocking(impl->socket_value);
  if (status == SALTS_OK &&
      bind(impl->socket_value, (const struct sockaddr *)address, (int)address_length) != 0)
    status = cnet_listener_native_error();
  if (status == SALTS_OK && listen(impl->socket_value, (int)config->backlog) != 0)
    status = cnet_listener_native_error();
  if (status == SALTS_OK) status = cnet_listener_bound_port(impl->socket_value, &impl->port);
  if (status != SALTS_OK) {
    cnet_listener_close_native(impl);
    free(impl);
    (void)cnet_module_shutdown();
    return status;
  }
  listener->impl = impl;
  return SALTS_OK;
}

int cnet_listener_init(cnet_listener *listener, const cnet_listener_config *config) {
  const cnet_listener_options options = CNET_LISTENER_OPTIONS_INIT;
  return cnet_listener_init_ex(listener, config, &options);
}

int cnet_listener_port(const cnet_listener *listener, uint16_t *out_port) {
  const cnet_listener_impl *impl = cnet_listener_const_get(listener);
  if (out_port == NULL) return SALTS_EINVAL;
  *out_port = 0u;
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->closed) return SALTS_ESHUTDOWN;
  *out_port = impl->port;
  return SALTS_OK;
}

int cnet_listener_wait(cnet_listener *listener, uint32_t timeout_ms, int *out_ready) {
  cnet_listener_impl *impl = cnet_listener_get(listener);
  int native_timeout = timeout_ms > (uint32_t)INT_MAX ? INT_MAX : (int)timeout_ms;
  int result;
  if (out_ready == NULL) return SALTS_EINVAL;
  *out_ready = 0;
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->closed) return SALTS_ESHUTDOWN;
#if defined(_WIN32)
  {
    WSAPOLLFD poll_fd = {impl->socket_value, POLLRDNORM, 0};
    result = WSAPoll(&poll_fd, 1u, native_timeout);
    if (result > 0 && (poll_fd.revents & (POLLRDNORM | POLLERR | POLLHUP | POLLNVAL)) != 0)
      *out_ready = 1;
  }
#else
  {
    struct pollfd poll_fd = {impl->socket_value, POLLIN, 0};
    const uint64_t started_ms = salts_monotonic_ms();
    for (;;) {
      poll_fd.revents = 0;
      result = poll(&poll_fd, 1u, native_timeout);
      if (result >= 0 || errno != EINTR) break;
      if (timeout_ms == 0u) {
        result = 0;
        break;
      }
      {
        const uint64_t elapsed_ms = salts_monotonic_ms() - started_ms;
        const uint64_t remaining_ms =
            elapsed_ms >= timeout_ms ? 0u : (uint64_t)timeout_ms - elapsed_ms;
        if (remaining_ms == 0u) {
          result = 0;
          break;
        }
        native_timeout = remaining_ms > (uint64_t)INT_MAX ? INT_MAX : (int)remaining_ms;
      }
    }
    if (result > 0 && (poll_fd.revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0)
      *out_ready = 1;
  }
#endif
  if (result < 0) return cnet_listener_native_error();
  return SALTS_OK;
}

int cnet_listener_accept(cnet_listener *listener, cnet_client *client,
                         const cnet_observer *observer, cnet_connection *out_connection) {
  cnet_stream_peer peer;
  return cnet_listener_accept_peer(listener, client, observer, out_connection, &peer);
}

int cnet_listener_accept_peer(cnet_listener *listener, cnet_client *client,
                              const cnet_observer *observer, cnet_connection *out_connection,
                              cnet_stream_peer *out_peer) {
  cnet_listener_impl *impl = cnet_listener_get(listener);
  struct sockaddr_storage native_peer;
#if defined(_WIN32)
  int native_peer_size = (int)sizeof(native_peer);
#else
  socklen_t native_peer_size = (socklen_t)sizeof(native_peer);
#endif
  cnet_listener_socket accepted;
  int status;
  if (out_connection == NULL) return SALTS_EINVAL;
  *out_connection = (cnet_connection){0};
  if (out_peer != NULL) *out_peer = (cnet_stream_peer){0};
  if (impl == NULL || client == NULL || observer == NULL || observer->on_state == NULL ||
      out_peer == NULL)
    return SALTS_EINVAL;
  if (impl->closed) return SALTS_ESHUTDOWN;
  memset(&native_peer, 0, sizeof(native_peer));
  do {
    accepted = accept(impl->socket_value, (struct sockaddr *)&native_peer, &native_peer_size);
#if defined(_WIN32)
  } while (false);
#else
  } while (accepted == CNET_LISTENER_INVALID_SOCKET && errno == EINTR);
#endif
  if (accepted == CNET_LISTENER_INVALID_SOCKET)
    return cnet_listener_would_block() ? SALTS_ETIMEDOUT : cnet_listener_native_error();
  status = cnet_listener_stream_peer(&native_peer, (size_t)native_peer_size, out_peer);
  if (status != SALTS_OK) {
    cnet_transport_close_socket((uintptr_t)accepted);
    return status;
  }
#if !defined(_WIN32)
  {
    status = cnet_listener_set_nonblocking(accepted);
    if (status != SALTS_OK) {
      cnet_transport_close_socket((uintptr_t)accepted);
      return status;
    }
  }
#endif
  status = cnet_client_adopt_tcp(client, (uintptr_t)accepted, observer, out_connection);
  if (status != SALTS_OK) *out_peer = (cnet_stream_peer){0};
  return status;
}

int cnet_listener_accept_tls(cnet_listener *listener, cnet_client *client,
                             const cnet_tls_server *server, const cnet_observer *observer,
                             cnet_connection *out_connection) {
  cnet_stream_peer peer;
  return cnet_listener_accept_tls_peer(listener, client, server, observer, out_connection, &peer);
}

int cnet_listener_accept_tls_peer(cnet_listener *listener, cnet_client *client,
                                  const cnet_tls_server *server,
                                  const cnet_observer *observer,
                                  cnet_connection *out_connection, cnet_stream_peer *out_peer) {
  cnet_listener_impl *impl = cnet_listener_get(listener);
  cnet_tls_context *context = cnet_tls_server_context(server);
  struct sockaddr_storage native_peer;
#if defined(_WIN32)
  int native_peer_size = (int)sizeof(native_peer);
#else
  socklen_t native_peer_size = (socklen_t)sizeof(native_peer);
#endif
  cnet_listener_socket accepted;
  int status;
  if (out_connection == NULL) return SALTS_EINVAL;
  *out_connection = (cnet_connection){0};
  if (out_peer != NULL) *out_peer = (cnet_stream_peer){0};
  if (impl == NULL || client == NULL || context == NULL || observer == NULL ||
      observer->on_state == NULL || out_peer == NULL)
    return SALTS_EINVAL;
  if (impl->closed) return SALTS_ESHUTDOWN;
  memset(&native_peer, 0, sizeof(native_peer));
  do {
    accepted = accept(impl->socket_value, (struct sockaddr *)&native_peer, &native_peer_size);
#if defined(_WIN32)
  } while (false);
#else
  } while (accepted == CNET_LISTENER_INVALID_SOCKET && errno == EINTR);
#endif
  if (accepted == CNET_LISTENER_INVALID_SOCKET)
    return cnet_listener_would_block() ? SALTS_ETIMEDOUT : cnet_listener_native_error();
  status = cnet_listener_stream_peer(&native_peer, (size_t)native_peer_size, out_peer);
  if (status != SALTS_OK) {
    cnet_transport_close_socket((uintptr_t)accepted);
    return status;
  }
#if !defined(_WIN32)
  {
    status = cnet_listener_set_nonblocking(accepted);
    if (status != SALTS_OK) {
      cnet_transport_close_socket((uintptr_t)accepted);
      return status;
    }
  }
#endif
  status = cnet_client_adopt_tls_server(client, (uintptr_t)accepted, context, observer,
                                        out_connection);
  if (status != SALTS_OK) *out_peer = (cnet_stream_peer){0};
  return status;
}

int cnet_listener_close(cnet_listener *listener) {
  cnet_listener_impl *impl = cnet_listener_get(listener);
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->closed) return SALTS_EALREADY;
  cnet_listener_close_native(impl);
  impl->closed = true;
  return SALTS_OK;
}

int cnet_listener_destroy(cnet_listener *listener) {
  cnet_listener_impl *impl = cnet_listener_get(listener);
  int status;
  if (listener == NULL) return SALTS_EINVAL;
  if (impl == NULL) return SALTS_OK;
  if (!impl->closed) return SALTS_EBUSY;
  free(impl);
  listener->impl = NULL;
  status = cnet_module_shutdown();
  return status;
}
