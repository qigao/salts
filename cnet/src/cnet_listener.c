#include <cnet/cnet.h>

#include "cnet_client_internal.h"
#include "cnet_module.h"
#include "cnet_transport.h"

#include <turbo/clock.h>

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
  if (error == WSAEADDRINUSE) return TURBO_EADDRINUSE;
  if (error == WSAEADDRNOTAVAIL) return TURBO_EADDRNOTAVAIL;
  if (error == WSAEAFNOSUPPORT) return TURBO_EAFNOSUPPORT;
  if (error == WSAEALREADY) return TURBO_EALREADY;
  if (error == WSAEBADF) return TURBO_EBADF;
  if (error == WSAEACCES) return TURBO_EPERM;
  if (error == WSAECONNABORTED) return TURBO_ECONNABORTED;
  if (error == WSAECONNREFUSED) return TURBO_ECONNREFUSED;
  if (error == WSAECONNRESET) return TURBO_ECONNRESET;
  if (error == WSAEDESTADDRREQ) return TURBO_EDESTADDRREQ;
  if (error == WSAEFAULT) return TURBO_EFAULT;
  if (error == WSAEHOSTUNREACH) return TURBO_EHOSTUNREACH;
  if (error == WSAEINTR) return TURBO_EINTR;
  if (error == WSAEINVAL) return TURBO_EINVAL;
  if (error == WSAEISCONN) return TURBO_EISCONN;
  if (error == WSAEMFILE) return TURBO_EMFILE;
  if (error == WSAEMSGSIZE) return TURBO_EMSGSIZE;
  if (error == WSAENETDOWN) return TURBO_ENETDOWN;
  if (error == WSAENETUNREACH) return TURBO_ENETUNREACH;
  if (error == WSAENOBUFS) return TURBO_ENOBUFS;
  if (error == WSAENOPROTOOPT) return TURBO_ENOPROTOOPT;
  if (error == WSAENOTCONN) return TURBO_ENOTCONN;
  if (error == WSAENOTSOCK) return TURBO_ENOTSOCK;
  if (error == WSAEOPNOTSUPP) return TURBO_ENOTSUP;
  if (error == WSAEPROTONOSUPPORT) return TURBO_EPROTONOSUPPORT;
  if (error == WSAEPROTOTYPE) return TURBO_EPROTOTYPE;
  if (error == WSAESHUTDOWN) return TURBO_ESHUTDOWN;
  if (error == WSAETIMEDOUT) return TURBO_ETIMEDOUT;
  if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS) return TURBO_EBUSY;
#else
  if (error == EADDRINUSE) return TURBO_EADDRINUSE;
  if (error == EADDRNOTAVAIL) return TURBO_EADDRNOTAVAIL;
  if (error == EAFNOSUPPORT) return TURBO_EAFNOSUPPORT;
  if (error == EALREADY) return TURBO_EALREADY;
  if (error == EBADF) return TURBO_EBADF;
  if (error == EBUSY) return TURBO_EBUSY;
  if (error == EACCES || error == EPERM) return TURBO_EPERM;
  if (error == ECONNABORTED) return TURBO_ECONNABORTED;
  if (error == ECONNREFUSED) return TURBO_ECONNREFUSED;
  if (error == ECONNRESET) return TURBO_ECONNRESET;
  if (error == EDESTADDRREQ) return TURBO_EDESTADDRREQ;
  if (error == EFAULT) return TURBO_EFAULT;
  if (error == EHOSTUNREACH) return TURBO_EHOSTUNREACH;
  if (error == EINTR) return TURBO_EINTR;
  if (error == EINVAL) return TURBO_EINVAL;
  if (error == EISCONN) return TURBO_EISCONN;
  if (error == EMFILE) return TURBO_EMFILE;
  if (error == EMSGSIZE) return TURBO_EMSGSIZE;
  if (error == ENETDOWN) return TURBO_ENETDOWN;
  if (error == ENETUNREACH) return TURBO_ENETUNREACH;
  if (error == ENFILE) return TURBO_ENFILE;
  if (error == ENOBUFS) return TURBO_ENOBUFS;
  if (error == ENOMEM) return TURBO_ENOMEM;
  if (error == ENOPROTOOPT) return TURBO_ENOPROTOOPT;
  if (error == ENOTCONN) return TURBO_ENOTCONN;
  if (error == ENOTSOCK) return TURBO_ENOTSOCK;
  if (error == EOPNOTSUPP) return TURBO_ENOTSUP;
  if (error == EPROTONOSUPPORT) return TURBO_EPROTONOSUPPORT;
  if (error == EPROTOTYPE) return TURBO_EPROTOTYPE;
  if (error == EPIPE) return TURBO_EPIPE;
  if (error == ETIMEDOUT) return TURBO_ETIMEDOUT;
  if (error == EAGAIN || error == EWOULDBLOCK || error == EINPROGRESS) return TURBO_EBUSY;
#endif
  return TURBO_EIO;
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
  if (ioctlsocket(socket_value, FIONBIO, &enabled) == 0) return TURBO_OK;
#else
  const int flags = fcntl(socket_value, F_GETFL, 0);
  if (flags >= 0 && fcntl(socket_value, F_SETFL, flags | O_NONBLOCK) == 0) return TURBO_OK;
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
  else return TURBO_EPROTO;
  return *out_port != 0u ? TURBO_OK : TURBO_EPROTO;
}

int cnet_listener_init(cnet_listener *listener, const cnet_listener_config *config) {
  cnet_listener_impl *impl;
  unsigned char address[CNET_LISTENER_ADDRESS_CAPACITY];
  size_t address_length = 0u;
  int family;
  int status;
#if !defined(_WIN32)
  const int reuse_address = 1;
#endif

  if (listener == NULL || config == NULL) return TURBO_EINVAL;
  if (listener->impl != NULL) return TURBO_EALREADY;
  if (config->host == NULL || config->backlog == 0u || config->backlog > INT_MAX ||
      !native_io_backend_kind_supported(config->backend))
    return TURBO_EINVAL;
  status = cnet_transport_parse_bind_address(config->host, config->port, address, sizeof(address),
                                             &address_length);
  if (status != TURBO_OK) return status;
  family = ((const struct sockaddr *)address)->sa_family;

  status = cnet_module_init();
  if (status != TURBO_OK) return status;
  impl = (cnet_listener_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) {
    (void)cnet_module_shutdown();
    return TURBO_ENOMEM;
  }
  impl->socket_value = CNET_LISTENER_INVALID_SOCKET;
  impl->backend = config->backend;
#if defined(_WIN32)
  if (config->backend != NATIVE_IO_BACKEND_IOCP) status = TURBO_ENOTSUP;
  else {
    impl->socket_value =
        WSASocketW(family, SOCK_STREAM, IPPROTO_TCP, NULL, 0u, WSA_FLAG_OVERLAPPED);
    status = impl->socket_value != CNET_LISTENER_INVALID_SOCKET ? TURBO_OK
                                                                : cnet_listener_native_error();
  }
#else
  impl->socket_value = socket(family, SOCK_STREAM, IPPROTO_TCP);
  status =
      impl->socket_value != CNET_LISTENER_INVALID_SOCKET ? TURBO_OK : cnet_listener_native_error();
  if (status == TURBO_OK && setsockopt(impl->socket_value, SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                                       (socklen_t)sizeof(reuse_address)) != 0)
    status = cnet_listener_native_error();
#endif
  if (status == TURBO_OK) status = cnet_listener_set_nonblocking(impl->socket_value);
  if (status == TURBO_OK &&
      bind(impl->socket_value, (const struct sockaddr *)address, (int)address_length) != 0)
    status = cnet_listener_native_error();
  if (status == TURBO_OK && listen(impl->socket_value, (int)config->backlog) != 0)
    status = cnet_listener_native_error();
  if (status == TURBO_OK) status = cnet_listener_bound_port(impl->socket_value, &impl->port);
  if (status != TURBO_OK) {
    cnet_listener_close_native(impl);
    free(impl);
    (void)cnet_module_shutdown();
    return status;
  }
  listener->impl = impl;
  return TURBO_OK;
}

int cnet_listener_port(const cnet_listener *listener, uint16_t *out_port) {
  const cnet_listener_impl *impl = cnet_listener_const_get(listener);
  if (out_port == NULL) return TURBO_EINVAL;
  *out_port = 0u;
  if (impl == NULL) return TURBO_EINVAL;
  if (impl->closed) return TURBO_ESHUTDOWN;
  *out_port = impl->port;
  return TURBO_OK;
}

int cnet_listener_wait(cnet_listener *listener, uint32_t timeout_ms, int *out_ready) {
  cnet_listener_impl *impl = cnet_listener_get(listener);
  int native_timeout = timeout_ms > (uint32_t)INT_MAX ? INT_MAX : (int)timeout_ms;
  int result;
  if (out_ready == NULL) return TURBO_EINVAL;
  *out_ready = 0;
  if (impl == NULL) return TURBO_EINVAL;
  if (impl->closed) return TURBO_ESHUTDOWN;
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
    const uint64_t started_ms = turbo_monotonic_ms();
    for (;;) {
      poll_fd.revents = 0;
      result = poll(&poll_fd, 1u, native_timeout);
      if (result >= 0 || errno != EINTR) break;
      if (timeout_ms == 0u) {
        result = 0;
        break;
      }
      {
        const uint64_t elapsed_ms = turbo_monotonic_ms() - started_ms;
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
  return TURBO_OK;
}

int cnet_listener_accept(cnet_listener *listener, cnet_client *client,
                         const cnet_observer *observer, cnet_connection *out_connection) {
  cnet_listener_impl *impl = cnet_listener_get(listener);
  cnet_listener_socket accepted;
  if (out_connection == NULL) return TURBO_EINVAL;
  *out_connection = (cnet_connection){0};
  if (impl == NULL || client == NULL || observer == NULL || observer->on_state == NULL)
    return TURBO_EINVAL;
  if (impl->closed) return TURBO_ESHUTDOWN;
  do {
    accepted = accept(impl->socket_value, NULL, NULL);
#if defined(_WIN32)
  } while (false);
#else
  } while (accepted == CNET_LISTENER_INVALID_SOCKET && errno == EINTR);
#endif
  if (accepted == CNET_LISTENER_INVALID_SOCKET)
    return cnet_listener_would_block() ? TURBO_ETIMEDOUT : cnet_listener_native_error();
#if !defined(_WIN32)
  {
    const int status = cnet_listener_set_nonblocking(accepted);
    if (status != TURBO_OK) {
      cnet_transport_close_socket((uintptr_t)accepted);
      return status;
    }
  }
#endif
  return cnet_client_adopt_tcp(client, (uintptr_t)accepted, observer, out_connection);
}

int cnet_listener_accept_tls(cnet_listener *listener, cnet_client *client,
                             const cnet_tls_server *server, const cnet_observer *observer,
                             cnet_connection *out_connection) {
  cnet_listener_impl *impl = cnet_listener_get(listener);
  cnet_tls_context *context = cnet_tls_server_context(server);
  cnet_listener_socket accepted;
  if (out_connection == NULL) return TURBO_EINVAL;
  *out_connection = (cnet_connection){0};
  if (impl == NULL || client == NULL || context == NULL || observer == NULL ||
      observer->on_state == NULL)
    return TURBO_EINVAL;
  if (impl->closed) return TURBO_ESHUTDOWN;
  do {
    accepted = accept(impl->socket_value, NULL, NULL);
#if defined(_WIN32)
  } while (false);
#else
  } while (accepted == CNET_LISTENER_INVALID_SOCKET && errno == EINTR);
#endif
  if (accepted == CNET_LISTENER_INVALID_SOCKET)
    return cnet_listener_would_block() ? TURBO_ETIMEDOUT : cnet_listener_native_error();
#if !defined(_WIN32)
  {
    const int status = cnet_listener_set_nonblocking(accepted);
    if (status != TURBO_OK) {
      cnet_transport_close_socket((uintptr_t)accepted);
      return status;
    }
  }
#endif
  return cnet_client_adopt_tls_server(client, (uintptr_t)accepted, context, observer,
                                      out_connection);
}

int cnet_listener_close(cnet_listener *listener) {
  cnet_listener_impl *impl = cnet_listener_get(listener);
  if (impl == NULL) return TURBO_EINVAL;
  if (impl->closed) return TURBO_EALREADY;
  cnet_listener_close_native(impl);
  impl->closed = true;
  return TURBO_OK;
}

int cnet_listener_destroy(cnet_listener *listener) {
  cnet_listener_impl *impl = cnet_listener_get(listener);
  int status;
  if (listener == NULL) return TURBO_EINVAL;
  if (impl == NULL) return TURBO_OK;
  if (!impl->closed) return TURBO_EBUSY;
  free(impl);
  listener->impl = NULL;
  status = cnet_module_shutdown();
  return status;
}
