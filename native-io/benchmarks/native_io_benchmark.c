#if !defined(_WIN32) && !defined(_GNU_SOURCE)
  #define _GNU_SOURCE
#endif

#include <turbo/clock.h>
#include <turbo/error_codes.h>
#include <turbo/native_io.h>

#include "tinytest.h"

#include <uv.h>

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET native_bench_socket;
typedef int native_bench_socklen;
  #define NATIVE_BENCH_INVALID_SOCKET INVALID_SOCKET
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <sys/socket.h>
  #include <unistd.h>
typedef int native_bench_socket;
typedef socklen_t native_bench_socklen;
  #define NATIVE_BENCH_INVALID_SOCKET (-1)
#endif

typedef enum native_bench_protocol { NATIVE_BENCH_TCP = 0, NATIVE_BENCH_UDP } native_bench_protocol;

typedef enum native_bench_driver {
  NATIVE_BENCH_LIBUV = 0,
  NATIVE_BENCH_NATIVE_IO
} native_bench_driver;

enum {
  NATIVE_BENCH_SAMPLES = 20,
  NATIVE_BENCH_EXCHANGES_PER_SAMPLE = 256,
  NATIVE_BENCH_WARMUP_EXCHANGES = 64,
  NATIVE_BENCH_TOTAL_EXCHANGES = NATIVE_BENCH_SAMPLES * NATIVE_BENCH_EXCHANGES_PER_SAMPLE,
  NATIVE_BENCH_TIMEOUT_MS = 5000,
  NATIVE_BENCH_ENDPOINT_CAPACITY = 2,
  NATIVE_BENCH_REQUEST_CAPACITY = 4,
  NATIVE_BENCH_COMPLETION_CAPACITY = 4
};

static const size_t NATIVE_BENCH_TCP_PAYLOADS[] = {1024u, 4096u, 8192u, 16384u, 32768u, 65536u};
/* 8 KiB is the largest common payload in this cross-platform UDP comparison. */
static const size_t NATIVE_BENCH_UDP_PAYLOADS[] = {1024u, 4096u, 8192u};

typedef struct native_bench_result {
  size_t payload_size;
  size_t round_trips;
  uint64_t wall_ns;
  uint64_t p50_ns;
  uint64_t p95_ns;
} native_bench_result;

typedef struct native_bench_socket_pair {
  native_bench_socket sockets[2];
  struct sockaddr_in addresses[2];
  bool network_started;
} native_bench_socket_pair;

typedef struct native_bench_native_fixture {
  native_bench_protocol protocol;
  native_bench_socket_pair pair;
  native_io_backend backend;
  native_io_endpoint endpoints[2];
} native_bench_native_fixture;

typedef struct native_bench_libuv_fixture {
  native_bench_protocol protocol;
  native_bench_socket_pair pair;
  uv_loop_t loop;
  uv_tcp_t tcp[2];
  uv_udp_t udp[2];
  uv_write_t tcp_writes[2];
  uv_udp_send_t udp_sends[2];
  bool loop_initialized;
  bool handles_initialized[2];
  bool reads_active[2];
  unsigned char *sent;
  unsigned char *server_received;
  unsigned char *client_received;
  size_t payload_size;
  size_t received[2];
  size_t pending_writes;
  int status;
  bool done;
  unsigned char overflow_byte;
} native_bench_libuv_fixture;

static bool native_bench_socket_valid(native_bench_socket socket_value) {
  return socket_value != NATIVE_BENCH_INVALID_SOCKET;
}

static int native_bench_socket_error(void) {
#ifdef _WIN32
  const int error = WSAGetLastError();
#else
  const int error = errno;
#endif
  return error == 0 ? TURBO_EIO : -error;
}

static int native_bench_network_start(native_bench_socket_pair *pair) {
#ifdef _WIN32
  WSADATA data;
  const int status = WSAStartup(MAKEWORD(2, 2), &data);
  if (status != 0) return -status;
#endif
  pair->network_started = true;
  return TURBO_OK;
}

static void native_bench_network_stop(native_bench_socket_pair *pair) {
#ifdef _WIN32
  if (pair->network_started) (void)WSACleanup();
#endif
  pair->network_started = false;
}

static native_bench_socket native_bench_socket_create(int type, int protocol) {
#ifdef _WIN32
  return WSASocketW(AF_INET, type, protocol, NULL, 0u, WSA_FLAG_OVERLAPPED);
#else
  return socket(AF_INET, type, protocol);
#endif
}

static int native_bench_socket_close(native_bench_socket socket_value) {
  if (!native_bench_socket_valid(socket_value)) return TURBO_OK;
#ifdef _WIN32
  return closesocket(socket_value) == 0 ? TURBO_OK : native_bench_socket_error();
#else
  return close(socket_value) == 0 ? TURBO_OK : native_bench_socket_error();
#endif
}

static int native_bench_set_socket_option(native_bench_socket socket_value, int level, int option,
                                          const void *value, size_t value_size) {
#ifdef _WIN32
  if (value_size > INT_MAX) return TURBO_ERANGE;
  return setsockopt(socket_value, level, option, (const char *)value, (int)value_size) == 0
             ? TURBO_OK
             : native_bench_socket_error();
#else
  return setsockopt(socket_value, level, option, value, (socklen_t)value_size) == 0
             ? TURBO_OK
             : native_bench_socket_error();
#endif
}

static int native_bench_set_nonblocking(native_bench_socket socket_value) {
#ifdef _WIN32
  (void)socket_value;
  return TURBO_OK;
#else
  const int flags = fcntl(socket_value, F_GETFL, 0);
  if (flags < 0) return native_bench_socket_error();
  if (fcntl(socket_value, F_SETFL, flags | O_NONBLOCK) != 0) return native_bench_socket_error();
  return TURBO_OK;
#endif
}

static int native_bench_bind_loopback(native_bench_socket socket_value,
                                      struct sockaddr_in *address) {
  native_bench_socklen address_length = (native_bench_socklen)sizeof(*address);
  memset(address, 0, sizeof(*address));
  address->sin_family = AF_INET;
  address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address->sin_port = 0;
  if (bind(socket_value, (const struct sockaddr *)address, sizeof(*address)) != 0)
    return native_bench_socket_error();
  if (getsockname(socket_value, (struct sockaddr *)address, &address_length) != 0)
    return native_bench_socket_error();
  return TURBO_OK;
}

static int native_bench_disable_sigpipe(native_bench_socket socket_value) {
#if defined(SO_NOSIGPIPE)
  const int enabled = 1;
  return native_bench_set_socket_option(socket_value, SOL_SOCKET, SO_NOSIGPIPE, &enabled,
                                        sizeof(enabled));
#else
  (void)socket_value;
  return TURBO_OK;
#endif
}

static void native_bench_socket_pair_reset(native_bench_socket_pair *pair) {
  memset(pair, 0, sizeof(*pair));
  pair->sockets[0] = NATIVE_BENCH_INVALID_SOCKET;
  pair->sockets[1] = NATIVE_BENCH_INVALID_SOCKET;
}

static int native_bench_make_tcp_pair(native_bench_socket_pair *pair) {
  native_bench_socket listener = NATIVE_BENCH_INVALID_SOCKET;
  struct sockaddr_in listener_address;
  const int no_delay = 1;
  int status;

  listener = native_bench_socket_create(SOCK_STREAM, IPPROTO_TCP);
  if (!native_bench_socket_valid(listener)) return native_bench_socket_error();
  status = native_bench_bind_loopback(listener, &listener_address);
  if (status == TURBO_OK && listen(listener, 1) != 0) status = native_bench_socket_error();
  if (status == TURBO_OK) {
    pair->sockets[0] = native_bench_socket_create(SOCK_STREAM, IPPROTO_TCP);
    if (!native_bench_socket_valid(pair->sockets[0])) status = native_bench_socket_error();
  }
  if (status == TURBO_OK && connect(pair->sockets[0], (const struct sockaddr *)&listener_address,
                                    sizeof(listener_address)) != 0)
    status = native_bench_socket_error();
  if (status == TURBO_OK) {
    pair->sockets[1] = accept(listener, NULL, NULL);
    if (!native_bench_socket_valid(pair->sockets[1])) status = native_bench_socket_error();
  }
  {
    const int close_status = native_bench_socket_close(listener);
    if (status == TURBO_OK) status = close_status;
  }
  if (status == TURBO_OK)
    status = native_bench_set_socket_option(pair->sockets[0], IPPROTO_TCP, TCP_NODELAY, &no_delay,
                                            sizeof(no_delay));
  if (status == TURBO_OK)
    status = native_bench_set_socket_option(pair->sockets[1], IPPROTO_TCP, TCP_NODELAY, &no_delay,
                                            sizeof(no_delay));
  if (status == TURBO_OK) status = native_bench_disable_sigpipe(pair->sockets[0]);
  if (status == TURBO_OK) status = native_bench_disable_sigpipe(pair->sockets[1]);
  return status;
}

static int native_bench_make_udp_pair(native_bench_socket_pair *pair) {
  int status = TURBO_OK;
  pair->sockets[0] = native_bench_socket_create(SOCK_DGRAM, IPPROTO_UDP);
  pair->sockets[1] = native_bench_socket_create(SOCK_DGRAM, IPPROTO_UDP);
  if (!native_bench_socket_valid(pair->sockets[0]) || !native_bench_socket_valid(pair->sockets[1]))
    status = native_bench_socket_error();
  if (status == TURBO_OK)
    status = native_bench_bind_loopback(pair->sockets[0], &pair->addresses[0]);
  if (status == TURBO_OK)
    status = native_bench_bind_loopback(pair->sockets[1], &pair->addresses[1]);
  return status;
}

static int native_bench_socket_pair_init(native_bench_socket_pair *pair,
                                         native_bench_protocol protocol) {
  int status;
  native_bench_socket_pair_reset(pair);
  status = native_bench_network_start(pair);
  if (status != TURBO_OK) return status;
  return protocol == NATIVE_BENCH_TCP ? native_bench_make_tcp_pair(pair)
                                      : native_bench_make_udp_pair(pair);
}

static int native_bench_socket_pair_destroy(native_bench_socket_pair *pair) {
  int status = TURBO_OK;
  for (size_t index = 0u; index < 2u; ++index) {
    const int close_status = native_bench_socket_close(pair->sockets[index]);
    pair->sockets[index] = NATIVE_BENCH_INVALID_SOCKET;
    if (status == TURBO_OK) status = close_status;
  }
  native_bench_network_stop(pair);
  return status;
}

static native_io_backend_kind native_bench_backend_kind(void) {
#ifdef _WIN32
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
  return NATIVE_IO_BACKEND_EPOLL;
#else
  return NATIVE_IO_BACKEND_KQUEUE;
#endif
}

static const char *native_bench_backend_name(void) {
#ifdef _WIN32
  return "IOCP";
#elif defined(__linux__)
  return "epoll";
#else
  return "kqueue";
#endif
}

static int native_bench_native_init(native_bench_native_fixture *fixture,
                                    native_bench_protocol protocol) {
  const native_io_backend_config config = {
      native_bench_backend_kind(), NATIVE_BENCH_ENDPOINT_CAPACITY, NATIVE_BENCH_REQUEST_CAPACITY,
      NATIVE_BENCH_COMPLETION_CAPACITY};
  int status;
  memset(fixture, 0, sizeof(*fixture));
  fixture->protocol = protocol;
  native_bench_socket_pair_reset(&fixture->pair);
  status = native_io_backend_init(&fixture->backend, &config);
  if (status != TURBO_OK) return status;
  status = native_bench_socket_pair_init(&fixture->pair, protocol);
  for (size_t index = 0u; status == TURBO_OK && index < 2u; ++index)
    status = native_bench_set_nonblocking(fixture->pair.sockets[index]);
  for (size_t index = 0u; status == TURBO_OK && index < 2u; ++index)
    status = native_io_backend_attach_socket(
        &fixture->backend, (uintptr_t)fixture->pair.sockets[index], &fixture->endpoints[index]);
  return status;
}

static int native_bench_native_destroy(native_bench_native_fixture *fixture) {
  int status = TURBO_OK;
  for (size_t index = 0u; index < 2u; ++index) {
    const int close_status = native_bench_socket_close(fixture->pair.sockets[index]);
    fixture->pair.sockets[index] = NATIVE_BENCH_INVALID_SOCKET;
    if (status == TURBO_OK) status = close_status;
    if (native_io_endpoint_valid(fixture->endpoints[index])) {
      const int release_status =
          native_io_backend_release_socket(&fixture->backend, fixture->endpoints[index]);
      fixture->endpoints[index] = (native_io_endpoint){0};
      if (status == TURBO_OK) status = release_status;
    }
  }
  native_bench_network_stop(&fixture->pair);
  if (fixture->backend.impl != NULL) {
    const int close_status = native_io_backend_close(&fixture->backend);
    const int destroy_status =
        close_status == TURBO_OK ? native_io_backend_destroy(&fixture->backend) : close_status;
    if (status == TURBO_OK) status = destroy_status;
  }
  return status;
}

static int native_bench_native_transfer(native_bench_native_fixture *fixture, size_t source_index,
                                        size_t destination_index, const unsigned char *sent,
                                        unsigned char *received, size_t length) {
  struct sockaddr_storage peer_address;
  size_t sent_offset = 0u;
  size_t received_offset = 0u;
  bool send_pending = false;
  bool receive_pending = false;

  while (sent_offset < length || received_offset < length) {
    native_io_completion events[NATIVE_BENCH_COMPLETION_CAPACITY];
    size_t event_count = 0u;
    int status;
    if (!receive_pending && received_offset < length) {
      native_io_operation operation = {.kind = fixture->protocol == NATIVE_BENCH_TCP
                                                   ? NATIVE_IO_OPERATION_TCP_RECV
                                                   : NATIVE_IO_OPERATION_UDP_RECV_FROM,
                                       .endpoint = fixture->endpoints[destination_index],
                                       .buffer = received + received_offset,
                                       .length = length - received_offset,
                                       .user_data = 1u};
      native_io_request request;
      if (fixture->protocol == NATIVE_BENCH_UDP) {
        operation.address = &peer_address;
        operation.address_capacity = sizeof(peer_address);
      }
      status = native_io_backend_submit(&fixture->backend, &operation, &request);
      if (status != TURBO_OK) return status;
      receive_pending = true;
    }
    if (!send_pending && sent_offset < length) {
      native_io_operation operation = {.kind = fixture->protocol == NATIVE_BENCH_TCP
                                                   ? NATIVE_IO_OPERATION_TCP_SEND
                                                   : NATIVE_IO_OPERATION_UDP_SEND_TO,
                                       .endpoint = fixture->endpoints[source_index],
                                       .buffer = (void *)(sent + sent_offset),
                                       .length = length - sent_offset,
                                       .user_data = 2u};
      native_io_request request;
      if (fixture->protocol == NATIVE_BENCH_UDP) {
        operation.address = &fixture->pair.addresses[destination_index];
        operation.address_capacity = sizeof(fixture->pair.addresses[destination_index]);
        operation.address_length = sizeof(fixture->pair.addresses[destination_index]);
      }
      status = native_io_backend_submit(&fixture->backend, &operation, &request);
      if (status != TURBO_OK) return status;
      send_pending = true;
    }

    status = native_io_backend_observe(&fixture->backend, events, NATIVE_BENCH_COMPLETION_CAPACITY,
                                       NATIVE_BENCH_TIMEOUT_MS, &event_count);
    if (status != TURBO_OK) return status;
    if (event_count == 0u) return TURBO_EIO;
    for (size_t index = 0u; index < event_count; ++index) {
      if (events[index].kind != NATIVE_IO_COMPLETION_OK || events[index].bytes == 0u)
        return events[index].status == TURBO_OK ? TURBO_EIO : events[index].status;
      if (events[index].user_data == 1u) {
        received_offset += events[index].bytes;
        receive_pending = false;
      } else if (events[index].user_data == 2u) {
        sent_offset += events[index].bytes;
        send_pending = false;
      } else {
        return TURBO_EPROTO;
      }
    }
    if (sent_offset > length || received_offset > length) return TURBO_EIO;
    if (fixture->protocol == NATIVE_BENCH_UDP &&
        ((sent_offset != 0u && sent_offset != length) ||
         (received_offset != 0u && received_offset != length)))
      return TURBO_EIO;
  }
  return TURBO_OK;
}

static int native_bench_native_exchange(native_bench_native_fixture *fixture,
                                        const unsigned char *sent, unsigned char *server_received,
                                        unsigned char *client_received, size_t length) {
  int status = native_bench_native_transfer(fixture, 0u, 1u, sent, server_received, length);
  if (status == TURBO_OK)
    status =
        native_bench_native_transfer(fixture, 1u, 0u, server_received, client_received, length);
  if (status == TURBO_OK && memcmp(sent, client_received, length) != 0) return TURBO_EIO;
  return status;
}

static void native_bench_libuv_fail(native_bench_libuv_fixture *fixture, int status) {
  if (fixture->status == TURBO_OK) fixture->status = status == 0 ? TURBO_EIO : status;
  fixture->done = true;
}

static bool native_bench_libuv_is_server(const native_bench_libuv_fixture *fixture,
                                         const uv_handle_t *handle) {
  if (fixture->protocol == NATIVE_BENCH_TCP) return handle == (const uv_handle_t *)&fixture->tcp[1];
  return handle == (const uv_handle_t *)&fixture->udp[1];
}

static void native_bench_libuv_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buffer) {
  native_bench_libuv_fixture *fixture = (native_bench_libuv_fixture *)handle->data;
  const size_t index = native_bench_libuv_is_server(fixture, handle) ? 1u : 0u;
  unsigned char *target = index == 1u ? fixture->server_received : fixture->client_received;
  const size_t remaining = fixture->received[index] < fixture->payload_size
                               ? fixture->payload_size - fixture->received[index]
                               : 0u;
  (void)suggested_size;
  if (remaining == 0u) {
    buffer->base = (char *)&fixture->overflow_byte;
    buffer->len = 1u;
    return;
  }
  buffer->base = (char *)(target + fixture->received[index]);
  buffer->len = remaining;
}

static void native_bench_libuv_try_finish(native_bench_libuv_fixture *fixture) {
  if (fixture->received[0] == fixture->payload_size && fixture->pending_writes == 0u)
    fixture->done = true;
}

static void native_bench_libuv_tcp_write(uv_write_t *request, int status) {
  native_bench_libuv_fixture *fixture = (native_bench_libuv_fixture *)request->data;
  if (fixture->pending_writes == 0u) {
    native_bench_libuv_fail(fixture, TURBO_EPROTO);
    return;
  }
  --fixture->pending_writes;
  if (status < 0) {
    native_bench_libuv_fail(fixture, status);
    return;
  }
  native_bench_libuv_try_finish(fixture);
}

static int native_bench_libuv_tcp_send(native_bench_libuv_fixture *fixture, size_t source_index,
                                       unsigned char *data) {
  uv_buf_t buffer = uv_buf_init((char *)data, (unsigned int)fixture->payload_size);
  uv_write_t *request = &fixture->tcp_writes[source_index];
  const int status = uv_write(request, (uv_stream_t *)&fixture->tcp[source_index], &buffer, 1u,
                              native_bench_libuv_tcp_write);
  if (status < 0) return status;
  request->data = fixture;
  ++fixture->pending_writes;
  return TURBO_OK;
}

static void native_bench_libuv_tcp_read(uv_stream_t *stream, ssize_t bytes,
                                        const uv_buf_t *buffer) {
  native_bench_libuv_fixture *fixture = (native_bench_libuv_fixture *)stream->data;
  const size_t index = stream == (uv_stream_t *)&fixture->tcp[1] ? 1u : 0u;
  int status;
  (void)buffer;
  if (bytes <= 0) {
    if (bytes < 0) native_bench_libuv_fail(fixture, (int)bytes);
    return;
  }
  if ((size_t)bytes > fixture->payload_size - fixture->received[index]) {
    native_bench_libuv_fail(fixture, TURBO_EIO);
    return;
  }
  fixture->received[index] += (size_t)bytes;
  if (fixture->received[index] != fixture->payload_size) return;
  status = uv_read_stop(stream);
  fixture->reads_active[index] = false;
  if (status < 0) {
    native_bench_libuv_fail(fixture, status);
    return;
  }
  if (index == 1u) {
    status = uv_read_start((uv_stream_t *)&fixture->tcp[0], native_bench_libuv_alloc,
                           native_bench_libuv_tcp_read);
    if (status < 0) {
      native_bench_libuv_fail(fixture, status);
      return;
    }
    fixture->reads_active[0] = true;
    status = native_bench_libuv_tcp_send(fixture, 1u, fixture->server_received);
    if (status != TURBO_OK) native_bench_libuv_fail(fixture, status);
  } else {
    native_bench_libuv_try_finish(fixture);
  }
}

static void native_bench_libuv_udp_send(uv_udp_send_t *request, int status) {
  native_bench_libuv_fixture *fixture = (native_bench_libuv_fixture *)request->data;
  if (fixture->pending_writes == 0u) {
    native_bench_libuv_fail(fixture, TURBO_EPROTO);
    return;
  }
  --fixture->pending_writes;
  if (status < 0) {
    native_bench_libuv_fail(fixture, status);
    return;
  }
  native_bench_libuv_try_finish(fixture);
}

static int native_bench_libuv_udp_write(native_bench_libuv_fixture *fixture, size_t source_index,
                                        size_t destination_index, unsigned char *data) {
  uv_buf_t buffer = uv_buf_init((char *)data, (unsigned int)fixture->payload_size);
  uv_udp_send_t *request = &fixture->udp_sends[source_index];
  const int status =
      uv_udp_send(request, &fixture->udp[source_index], &buffer, 1u,
                  (const struct sockaddr *)&fixture->pair.addresses[destination_index],
                  native_bench_libuv_udp_send);
  if (status < 0) return status;
  request->data = fixture;
  ++fixture->pending_writes;
  return TURBO_OK;
}

static void native_bench_libuv_udp_read(uv_udp_t *handle, ssize_t bytes, const uv_buf_t *buffer,
                                        const struct sockaddr *address, unsigned flags) {
  native_bench_libuv_fixture *fixture = (native_bench_libuv_fixture *)handle->data;
  const size_t index = handle == &fixture->udp[1] ? 1u : 0u;
  int status;
  (void)buffer;
  (void)address;
  if (bytes <= 0) {
    if (bytes < 0) native_bench_libuv_fail(fixture, (int)bytes);
    return;
  }
  if ((flags & UV_UDP_PARTIAL) != 0u || (size_t)bytes != fixture->payload_size ||
      fixture->received[index] != 0u) {
    native_bench_libuv_fail(fixture, TURBO_EIO);
    return;
  }
  fixture->received[index] = (size_t)bytes;
  status = uv_udp_recv_stop(handle);
  fixture->reads_active[index] = false;
  if (status < 0) {
    native_bench_libuv_fail(fixture, status);
    return;
  }
  if (index == 1u) {
    status =
        uv_udp_recv_start(&fixture->udp[0], native_bench_libuv_alloc, native_bench_libuv_udp_read);
    if (status < 0) {
      native_bench_libuv_fail(fixture, status);
      return;
    }
    fixture->reads_active[0] = true;
    status = native_bench_libuv_udp_write(fixture, 1u, 0u, fixture->server_received);
    if (status != TURBO_OK) native_bench_libuv_fail(fixture, status);
  } else {
    native_bench_libuv_try_finish(fixture);
  }
}

static int native_bench_libuv_init(native_bench_libuv_fixture *fixture,
                                   native_bench_protocol protocol) {
  int status;
  memset(fixture, 0, sizeof(*fixture));
  fixture->protocol = protocol;
  native_bench_socket_pair_reset(&fixture->pair);
  status = native_bench_socket_pair_init(&fixture->pair, protocol);
  if (status != TURBO_OK) return status;
  status = uv_loop_init(&fixture->loop);
  if (status < 0) return status;
  fixture->loop_initialized = true;
  for (size_t index = 0u; index < 2u; ++index) {
    if (protocol == NATIVE_BENCH_TCP) status = uv_tcp_init(&fixture->loop, &fixture->tcp[index]);
    else status = uv_udp_init(&fixture->loop, &fixture->udp[index]);
    if (status < 0) return status;
    fixture->handles_initialized[index] = true;
    if (protocol == NATIVE_BENCH_TCP) {
      fixture->tcp[index].data = fixture;
      status = uv_tcp_open(&fixture->tcp[index], (uv_os_sock_t)fixture->pair.sockets[index]);
    } else {
      fixture->udp[index].data = fixture;
      status = uv_udp_open(&fixture->udp[index], (uv_os_sock_t)fixture->pair.sockets[index]);
    }
    if (status < 0) return status;
    fixture->pair.sockets[index] = NATIVE_BENCH_INVALID_SOCKET;
  }
  return TURBO_OK;
}

static int native_bench_libuv_destroy(native_bench_libuv_fixture *fixture) {
  int status = TURBO_OK;
  if (fixture->loop_initialized) {
    for (size_t index = 0u; index < 2u; ++index) {
      if (fixture->reads_active[index]) {
        const int stop_status = fixture->protocol == NATIVE_BENCH_TCP
                                    ? uv_read_stop((uv_stream_t *)&fixture->tcp[index])
                                    : uv_udp_recv_stop(&fixture->udp[index]);
        if (status == TURBO_OK && stop_status < 0) status = stop_status;
        fixture->reads_active[index] = false;
      }
      if (fixture->handles_initialized[index]) {
        uv_handle_t *handle = fixture->protocol == NATIVE_BENCH_TCP
                                  ? (uv_handle_t *)&fixture->tcp[index]
                                  : (uv_handle_t *)&fixture->udp[index];
        if (!uv_is_closing(handle)) uv_close(handle, NULL);
      }
    }
    (void)uv_run(&fixture->loop, UV_RUN_DEFAULT);
    {
      const int close_status = uv_loop_close(&fixture->loop);
      if (status == TURBO_OK && close_status < 0) status = close_status;
    }
    fixture->loop_initialized = false;
  }
  {
    const int socket_status = native_bench_socket_pair_destroy(&fixture->pair);
    if (status == TURBO_OK) status = socket_status;
  }
  return status;
}

static int native_bench_libuv_exchange(native_bench_libuv_fixture *fixture,
                                       const unsigned char *sent, unsigned char *server_received,
                                       unsigned char *client_received, size_t length) {
  int status;
  fixture->sent = (unsigned char *)sent;
  fixture->server_received = server_received;
  fixture->client_received = client_received;
  fixture->payload_size = length;
  fixture->received[0] = 0u;
  fixture->received[1] = 0u;
  fixture->pending_writes = 0u;
  fixture->status = TURBO_OK;
  fixture->done = false;

  if (fixture->protocol == NATIVE_BENCH_TCP) {
    status = uv_read_start((uv_stream_t *)&fixture->tcp[1], native_bench_libuv_alloc,
                           native_bench_libuv_tcp_read);
    if (status < 0) return status;
    fixture->reads_active[1] = true;
    status = native_bench_libuv_tcp_send(fixture, 0u, fixture->sent);
  } else {
    status =
        uv_udp_recv_start(&fixture->udp[1], native_bench_libuv_alloc, native_bench_libuv_udp_read);
    if (status < 0) return status;
    fixture->reads_active[1] = true;
    status = native_bench_libuv_udp_write(fixture, 0u, 1u, fixture->sent);
  }
  if (status != TURBO_OK) return status;

  while (!fixture->done) {
    const int active = uv_run(&fixture->loop, UV_RUN_ONCE);
    if (!fixture->done && active == 0) return TURBO_EIO;
  }
  if (fixture->status != TURBO_OK) return fixture->status;
  if (memcmp(sent, client_received, length) != 0) return TURBO_EIO;
  return TURBO_OK;
}

static int native_bench_u64_compare(const void *left, const void *right) {
  const uint64_t lhs = *(const uint64_t *)left;
  const uint64_t rhs = *(const uint64_t *)right;
  return lhs < rhs ? -1 : lhs > rhs;
}

static int native_bench_run(native_bench_protocol protocol, native_bench_driver driver,
                            size_t payload_size, native_bench_result *result) {
  native_bench_native_fixture native_fixture;
  native_bench_libuv_fixture libuv_fixture;
  unsigned char *sent = NULL;
  unsigned char *server_received = NULL;
  unsigned char *client_received = NULL;
  uint64_t *latencies = NULL;
  uint64_t wall_started;
  size_t latency_count = 0u;
  const char *phase = "init";
  int status;

  memset(result, 0, sizeof(*result));
  memset(&native_fixture, 0, sizeof(native_fixture));
  memset(&libuv_fixture, 0, sizeof(libuv_fixture));
  if (driver == NATIVE_BENCH_LIBUV) status = native_bench_libuv_init(&libuv_fixture, protocol);
  else status = native_bench_native_init(&native_fixture, protocol);
  if (status != TURBO_OK) goto cleanup;

  phase = "allocate";
  sent = (unsigned char *)malloc(payload_size);
  server_received = (unsigned char *)malloc(payload_size);
  client_received = (unsigned char *)malloc(payload_size);
  latencies = (uint64_t *)malloc(sizeof(*latencies) * NATIVE_BENCH_TOTAL_EXCHANGES);
  if (sent == NULL || server_received == NULL || client_received == NULL || latencies == NULL) {
    status = TURBO_ENOMEM;
    goto cleanup;
  }
  memset(sent, 0x5a, payload_size);

  phase = "warmup";
  for (size_t index = 0u; index < NATIVE_BENCH_WARMUP_EXCHANGES; ++index) {
    status = driver == NATIVE_BENCH_LIBUV
                 ? native_bench_libuv_exchange(&libuv_fixture, sent, server_received,
                                               client_received, payload_size)
                 : native_bench_native_exchange(&native_fixture, sent, server_received,
                                                client_received, payload_size);
    if (status != TURBO_OK) goto cleanup;
  }

  phase = "measure";
  wall_started = turbo_hrtime();
  for (size_t sample = 0u; sample < NATIVE_BENCH_SAMPLES; ++sample) {
    for (size_t exchange = 0u; exchange < NATIVE_BENCH_EXCHANGES_PER_SAMPLE; ++exchange) {
      const uint64_t started = turbo_hrtime();
      status = driver == NATIVE_BENCH_LIBUV
                   ? native_bench_libuv_exchange(&libuv_fixture, sent, server_received,
                                                 client_received, payload_size)
                   : native_bench_native_exchange(&native_fixture, sent, server_received,
                                                  client_received, payload_size);
      if (status != TURBO_OK) goto cleanup;
      latencies[latency_count++] = turbo_hrtime() - started;
    }
  }
  result->payload_size = payload_size;
  result->round_trips = latency_count;
  result->wall_ns = turbo_hrtime() - wall_started;
  qsort(latencies, latency_count, sizeof(latencies[0]), native_bench_u64_compare);
  result->p50_ns = latencies[(latency_count - 1u) * 50u / 100u];
  result->p95_ns = latencies[(latency_count - 1u) * 95u / 100u];
  status = TURBO_OK;

cleanup:
  free(latencies);
  free(client_received);
  free(server_received);
  free(sent);
  {
    const int cleanup_status = driver == NATIVE_BENCH_LIBUV
                                   ? native_bench_libuv_destroy(&libuv_fixture)
                                   : native_bench_native_destroy(&native_fixture);
    if (status == TURBO_OK) status = cleanup_status;
  }
  if (status != TURBO_OK)
    fprintf(stderr, "benchmark driver=%s protocol=%s payload=%zu phase=%s status=%d\n",
            driver == NATIVE_BENCH_LIBUV ? "libuv" : "NativeIO",
            protocol == NATIVE_BENCH_TCP ? "TCP" : "UDP", payload_size, phase, status);
  return status;
}

static double native_bench_delta(double candidate, double baseline) {
  return baseline == 0.0 ? 0.0 : (candidate / baseline - 1.0) * 100.0;
}

static double native_bench_round_trips_per_second(const native_bench_result *result) {
  return result->wall_ns == 0u
             ? 0.0
             : (double)result->round_trips * 1000000000.0 / (double)result->wall_ns;
}

static double native_bench_mib_per_second(const native_bench_result *result) {
  const double bytes = (double)result->payload_size * 2.0 * (double)result->round_trips;
  return result->wall_ns == 0u ? 0.0
                               : bytes * 1000000000.0 / (double)result->wall_ns / (1024.0 * 1024.0);
}

static void native_bench_print_tables(const char *protocol, const native_bench_result *libuv,
                                      const native_bench_result *native, size_t count) {
  printf("\n%s persistent loopback ping-pong\n", protocol);
  printf("| payload | libuv p50 us | NativeIO p50 us | latency delta | "
         "libuv p95 us | NativeIO p95 us | latency delta |\n");
  printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    printf("| %zu KiB | %.3f | %.3f | %+.2f%% | %.3f | %.3f | %+.2f%% |\n",
           libuv[index].payload_size / 1024u, (double)libuv[index].p50_ns / 1000.0,
           (double)native[index].p50_ns / 1000.0,
           native_bench_delta((double)native[index].p50_ns, (double)libuv[index].p50_ns),
           (double)libuv[index].p95_ns / 1000.0, (double)native[index].p95_ns / 1000.0,
           native_bench_delta((double)native[index].p95_ns, (double)libuv[index].p95_ns));
  }

  printf("\n%s ping-pong rate and bidirectional goodput\n", protocol);
  printf("| payload | libuv round trips/s | NativeIO round trips/s | rate delta | "
         "libuv MiB/s | NativeIO MiB/s | goodput delta |\n");
  printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    const double libuv_rate = native_bench_round_trips_per_second(&libuv[index]);
    const double native_rate = native_bench_round_trips_per_second(&native[index]);
    const double libuv_goodput = native_bench_mib_per_second(&libuv[index]);
    const double native_goodput = native_bench_mib_per_second(&native[index]);
    printf("| %zu KiB | %.0f | %.0f | %+.2f%% | %.2f | %.2f | %+.2f%% |\n",
           libuv[index].payload_size / 1024u, libuv_rate, native_rate,
           native_bench_delta(native_rate, libuv_rate), libuv_goodput, native_goodput,
           native_bench_delta(native_goodput, libuv_goodput));
  }
}

static int native_bench_run_pair(native_bench_protocol protocol, size_t payload_size,
                                 size_t payload_index, native_bench_result *libuv,
                                 native_bench_result *native) {
  int status;
  /* Alternate first runner to reduce fixed ordering bias between payload rows. */
  if ((payload_index & 1u) == 0u) {
    status = native_bench_run(protocol, NATIVE_BENCH_LIBUV, payload_size, libuv);
    if (status == TURBO_OK)
      status = native_bench_run(protocol, NATIVE_BENCH_NATIVE_IO, payload_size, native);
  } else {
    status = native_bench_run(protocol, NATIVE_BENCH_NATIVE_IO, payload_size, native);
    if (status == TURBO_OK)
      status = native_bench_run(protocol, NATIVE_BENCH_LIBUV, payload_size, libuv);
  }
  return status;
}

spec("NativeIO versus libuv benchmark") {
  it("compares persistent TCP and UDP ping-pong workloads") {
    native_bench_result
        libuv_tcp[sizeof(NATIVE_BENCH_TCP_PAYLOADS) / sizeof(NATIVE_BENCH_TCP_PAYLOADS[0])] = {0};
    native_bench_result
        native_tcp[sizeof(NATIVE_BENCH_TCP_PAYLOADS) / sizeof(NATIVE_BENCH_TCP_PAYLOADS[0])] = {0};
    native_bench_result
        libuv_udp[sizeof(NATIVE_BENCH_UDP_PAYLOADS) / sizeof(NATIVE_BENCH_UDP_PAYLOADS[0])] = {0};
    native_bench_result
        native_udp[sizeof(NATIVE_BENCH_UDP_PAYLOADS) / sizeof(NATIVE_BENCH_UDP_PAYLOADS[0])] = {0};
    const size_t tcp_count =
        sizeof(NATIVE_BENCH_TCP_PAYLOADS) / sizeof(NATIVE_BENCH_TCP_PAYLOADS[0]);
    const size_t udp_count =
        sizeof(NATIVE_BENCH_UDP_PAYLOADS) / sizeof(NATIVE_BENCH_UDP_PAYLOADS[0]);

    printf("\nBaseline: libuv %s; NativeIO backend: %s\n", uv_version_string(),
           native_bench_backend_name());
    printf("Workload: %d warmups, then %d x %d persistent round trips per row.\n",
           NATIVE_BENCH_WARMUP_EXCHANGES, NATIVE_BENCH_SAMPLES, NATIVE_BENCH_EXCHANGES_PER_SAMPLE);
    printf("Positive latency delta is slower; positive rate/goodput delta is faster.\n");

    for (size_t index = 0u; index < tcp_count; ++index)
      check_equal(native_bench_run_pair(NATIVE_BENCH_TCP, NATIVE_BENCH_TCP_PAYLOADS[index], index,
                                        &libuv_tcp[index], &native_tcp[index]),
                  TURBO_OK);
    for (size_t index = 0u; index < udp_count; ++index)
      check_equal(native_bench_run_pair(NATIVE_BENCH_UDP, NATIVE_BENCH_UDP_PAYLOADS[index], index,
                                        &libuv_udp[index], &native_udp[index]),
                  TURBO_OK);

    native_bench_print_tables("TCP", libuv_tcp, native_tcp, tcp_count);
    native_bench_print_tables("UDP", libuv_udp, native_udp, udp_count);
    printf("\nUDP stops at 8 KiB so Windows, Linux, and macOS use one common "
           "single-datagram workload.\n");
  }
}
