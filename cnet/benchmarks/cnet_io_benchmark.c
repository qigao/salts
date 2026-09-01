#if !defined(_WIN32) && !defined(_GNU_SOURCE)
  #define _GNU_SOURCE
#endif

#include <cnet/cnet.h>
#include <turbo/clock.h>
#include <turbo/error_codes.h>
#include <turbo/native_io.h>
#include <turbo/thread.h>

#include "cnet_benchmark_stats.h"
#include "tinytest.h"

#include <uv.h>

#include <limits.h>
#include <stdatomic.h>
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
typedef SOCKET io_bench_socket;
typedef int io_bench_socklen;
  #define IO_BENCH_INVALID_SOCKET INVALID_SOCKET
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
typedef int io_bench_socket;
typedef socklen_t io_bench_socklen;
  #define IO_BENCH_INVALID_SOCKET (-1)
#endif

typedef enum io_bench_protocol { IO_BENCH_TCP = 0, IO_BENCH_UDP } io_bench_protocol;
typedef enum io_bench_driver {
  IO_BENCH_LIBUV = 0,
  IO_BENCH_NATIVE_IO,
  IO_BENCH_NATIVE_IO_COROUTINE,
  IO_BENCH_CNET
} io_bench_driver;

enum {
  IO_BENCH_REPLICATES = 5,
  IO_BENCH_EXCHANGES_PER_REPLICATE = 512,
  IO_BENCH_WARMUP_EXCHANGES = 32,
  IO_BENCH_TOTAL_EXCHANGES = IO_BENCH_EXCHANGES_PER_REPLICATE,
  IO_BENCH_ALL_EXCHANGES = IO_BENCH_WARMUP_EXCHANGES + IO_BENCH_TOTAL_EXCHANGES,
  IO_BENCH_TIMEOUT_MS = 5000,
  IO_BENCH_MAX_PAYLOAD = 65536,
  IO_BENCH_COMPLETION_CAPACITY = 4
};

static const size_t IO_BENCH_TCP_PAYLOADS[] = {1024u, 4096u, 8192u, 16384u, 32768u, 65536u};
static const size_t IO_BENCH_UDP_PAYLOADS[] = {1024u, 4096u, 8192u};

typedef struct io_bench_result {
  size_t payload_size;
  size_t round_trips;
  uint64_t wall_ns;
  uint64_t p50_ns;
  uint64_t p95_ns;
  uint64_t cnet_receive_admission_ns;
  uint64_t cnet_send_admission_ns;
  uint64_t cnet_poll_ns;
  uint64_t cnet_callback_ns;
  uint64_t cnet_payload_validation_ns;
  size_t cnet_receive_admission_calls;
  size_t cnet_send_admission_calls;
  size_t cnet_poll_calls;
  size_t cnet_callback_calls;
} io_bench_result;

typedef struct io_bench_series {
  size_t payload_size;
  io_bench_result runs[IO_BENCH_REPLICATES];
  io_bench_result cnet_profile_runs[IO_BENCH_REPLICATES];
  cnet_benchmark_summary p50_ns;
  cnet_benchmark_summary p95_ns;
  cnet_benchmark_summary rate_per_second;
  cnet_benchmark_summary cnet_send_admission_ns;
  cnet_benchmark_summary cnet_poll_ns;
  cnet_benchmark_summary cnet_callback_control_ns;
  cnet_benchmark_summary cnet_payload_validation_ns;
  cnet_benchmark_summary cnet_polls_per_round_trip;
} io_bench_series;

typedef struct io_bench_server {
  io_bench_protocol protocol;
  io_bench_socket socket_value;
  struct sockaddr_in address;
  size_t payload_size;
  size_t exchange_count;
  turbo_thread_t thread;
  atomic_int status;
  bool thread_started;
  bool socket_closed;
} io_bench_server;

typedef struct io_bench_native {
  io_bench_protocol protocol;
  io_bench_socket socket_value;
  native_io_backend backend;
  native_io_endpoint endpoint;
} io_bench_native;

typedef struct io_bench_native_coroutine_operation {
  io_bench_native *fixture;
  unsigned char *buffer;
  size_t length;
  size_t offset;
  int status;
  bool send;
  bool done;
} io_bench_native_coroutine_operation;

typedef struct io_bench_libuv {
  io_bench_protocol protocol;
  uv_loop_t loop;
  uv_tcp_t tcp;
  uv_udp_t udp;
  uv_write_t tcp_write;
  uv_udp_send_t udp_write;
  unsigned char *received_data;
  size_t payload_size;
  size_t received;
  int status;
  bool loop_initialized;
  bool handle_initialized;
  bool read_active;
  bool write_pending;
  bool done;
  unsigned char overflow;
} io_bench_libuv;

typedef struct io_bench_cnet {
  cnet_client client;
  cnet_connection connection;
  io_bench_protocol protocol;
  const unsigned char *expected_data;
  size_t payload_size;
  size_t received;
  int connected;
  int done;
  int terminal;
  int status;
  uint64_t receive_admission_ns;
  uint64_t send_admission_ns;
  uint64_t poll_ns;
  uint64_t callback_ns;
  uint64_t payload_validation_ns;
  size_t receive_admission_calls;
  size_t send_admission_calls;
  size_t poll_calls;
  size_t callback_calls;
  bool measuring;
} io_bench_cnet;

typedef struct io_bench_fixture {
  io_bench_driver driver;
  io_bench_protocol protocol;
  io_bench_server server;
  io_bench_native native;
  io_bench_libuv libuv;
  io_bench_cnet cnet;
  bool network_started;
} io_bench_fixture;

static bool io_bench_socket_valid(io_bench_socket value) {
  return value != IO_BENCH_INVALID_SOCKET;
}

static int io_bench_socket_error(void) {
#ifdef _WIN32
  const int error = WSAGetLastError();
#else
  const int error = errno;
#endif
  return error == 0 ? TURBO_EIO : -error;
}

static int io_bench_network_start(io_bench_fixture *fixture) {
#ifdef _WIN32
  WSADATA data;
  const int status = WSAStartup(MAKEWORD(2, 2), &data);
  if (status != 0) return -status;
#endif
  fixture->network_started = true;
  return TURBO_OK;
}

static void io_bench_network_stop(io_bench_fixture *fixture) {
#ifdef _WIN32
  if (fixture->network_started) (void)WSACleanup();
#endif
  fixture->network_started = false;
}

static int io_bench_socket_close(io_bench_socket value) {
  if (!io_bench_socket_valid(value)) return TURBO_OK;
#ifdef _WIN32
  return closesocket(value) == 0 ? TURBO_OK : io_bench_socket_error();
#else
  return close(value) == 0 ? TURBO_OK : io_bench_socket_error();
#endif
}

static int io_bench_set_option(io_bench_socket value, int level, int option, const void *data,
                               size_t size) {
#ifdef _WIN32
  if (size > INT_MAX) return TURBO_ERANGE;
  return setsockopt(value, level, option, (const char *)data, (int)size) == 0
             ? TURBO_OK
             : io_bench_socket_error();
#else
  return setsockopt(value, level, option, data, (socklen_t)size) == 0 ? TURBO_OK
                                                                      : io_bench_socket_error();
#endif
}

static int io_bench_set_timeout(io_bench_socket value) {
#ifdef _WIN32
  const DWORD timeout = IO_BENCH_TIMEOUT_MS;
  return io_bench_set_option(value, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#else
  const struct timeval timeout = {IO_BENCH_TIMEOUT_MS / 1000, (IO_BENCH_TIMEOUT_MS % 1000) * 1000};
  return io_bench_set_option(value, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
}

static int io_bench_set_nonblocking(io_bench_socket value) {
#ifdef _WIN32
  u_long enabled = 1u;
  return ioctlsocket(value, FIONBIO, &enabled) == 0 ? TURBO_OK : io_bench_socket_error();
#else
  const int flags = fcntl(value, F_GETFL, 0);
  if (flags < 0 || fcntl(value, F_SETFL, flags | O_NONBLOCK) != 0) return io_bench_socket_error();
  return TURBO_OK;
#endif
}

static int io_bench_disable_sigpipe(io_bench_socket value) {
#if defined(SO_NOSIGPIPE)
  const int enabled = 1;
  return io_bench_set_option(value, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#else
  (void)value;
  return TURBO_OK;
#endif
}

static int io_bench_bind_loopback(io_bench_socket value, struct sockaddr_in *address) {
  io_bench_socklen length = (io_bench_socklen)sizeof(*address);
  memset(address, 0, sizeof(*address));
  address->sin_family = AF_INET;
  address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address->sin_port = 0;
  if (bind(value, (const struct sockaddr *)address, sizeof(*address)) != 0)
    return io_bench_socket_error();
  if (getsockname(value, (struct sockaddr *)address, &length) != 0) return io_bench_socket_error();
  return TURBO_OK;
}

static int io_bench_send_all(io_bench_socket value, const unsigned char *data, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
#ifdef _WIN32
    const int sent = send(value, (const char *)data + offset, (int)(size - offset), 0);
#elif defined(MSG_NOSIGNAL)
    const ssize_t sent = send(value, data + offset, size - offset, MSG_NOSIGNAL);
#else
    const ssize_t sent = send(value, data + offset, size - offset, 0);
#endif
    if (sent <= 0) return io_bench_socket_error();
    offset += (size_t)sent;
  }
  return TURBO_OK;
}

static int io_bench_receive_all(io_bench_socket value, unsigned char *data, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
#ifdef _WIN32
    const int received = recv(value, (char *)data + offset, (int)(size - offset), 0);
#else
    const ssize_t received = recv(value, data + offset, size - offset, 0);
#endif
    if (received <= 0) return received == 0 ? TURBO_EIO : io_bench_socket_error();
    offset += (size_t)received;
  }
  return TURBO_OK;
}

static void io_bench_server_entry(void *argument) {
  io_bench_server *server = (io_bench_server *)argument;
  io_bench_socket active = server->socket_value;
  unsigned char *buffer = (unsigned char *)malloc(server->payload_size);
  int status = buffer == NULL ? TURBO_ENOMEM : TURBO_OK;
  if (status == TURBO_OK && server->protocol == IO_BENCH_TCP) {
    const int no_delay = 1;
    active = accept(server->socket_value, NULL, NULL);
    if (!io_bench_socket_valid(active)) status = io_bench_socket_error();
    if (status == TURBO_OK) status = io_bench_set_timeout(active);
    if (status == TURBO_OK)
      status = io_bench_set_option(active, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay));
  }
  for (size_t index = 0u; status == TURBO_OK && index < server->exchange_count; ++index) {
    if (server->protocol == IO_BENCH_TCP) {
      status = io_bench_receive_all(active, buffer, server->payload_size);
      if (status == TURBO_OK) status = io_bench_send_all(active, buffer, server->payload_size);
    } else {
      struct sockaddr_storage peer;
      io_bench_socklen peer_length = (io_bench_socklen)sizeof(peer);
#ifdef _WIN32
      const int received = recvfrom(active, (char *)buffer, (int)server->payload_size, 0,
                                    (struct sockaddr *)&peer, &peer_length);
#else
      const ssize_t received =
          recvfrom(active, buffer, server->payload_size, 0, (struct sockaddr *)&peer, &peer_length);
#endif
      if (received != (int)server->payload_size)
        status = received < 0 ? io_bench_socket_error() : TURBO_EIO;
      if (status == TURBO_OK) {
#ifdef _WIN32
        const int sent = sendto(active, (const char *)buffer, received, 0,
                                (const struct sockaddr *)&peer, peer_length);
#else
        const ssize_t sent = sendto(active, buffer, (size_t)received, 0,
                                    (const struct sockaddr *)&peer, peer_length);
#endif
        if (sent != received) status = sent < 0 ? io_bench_socket_error() : TURBO_EIO;
      }
    }
  }
  if (server->protocol == IO_BENCH_TCP) (void)io_bench_socket_close(active);
  free(buffer);
  atomic_store_explicit(&server->status, status, memory_order_release);
}

static int io_bench_server_init(io_bench_server *server, io_bench_protocol protocol,
                                size_t payload_size) {
  const int type = protocol == IO_BENCH_TCP ? SOCK_STREAM : SOCK_DGRAM;
  const int socket_protocol = protocol == IO_BENCH_TCP ? IPPROTO_TCP : IPPROTO_UDP;
  int status;
  memset(server, 0, sizeof(*server));
  server->protocol = protocol;
  server->payload_size = payload_size;
  server->exchange_count = IO_BENCH_ALL_EXCHANGES;
  server->socket_value = socket(AF_INET, type, socket_protocol);
  if (!io_bench_socket_valid(server->socket_value)) return io_bench_socket_error();
  status = io_bench_bind_loopback(server->socket_value, &server->address);
  if (status == TURBO_OK) status = io_bench_set_timeout(server->socket_value);
  if (status == TURBO_OK) status = io_bench_disable_sigpipe(server->socket_value);
  if (status == TURBO_OK && protocol == IO_BENCH_TCP && listen(server->socket_value, 1) != 0)
    status = io_bench_socket_error();
  atomic_init(&server->status, TURBO_OK);
  return status;
}

static int io_bench_server_start(io_bench_server *server) {
  const int status = turbo_thread_create(&server->thread, io_bench_server_entry, server);
  if (status == TURBO_OK) server->thread_started = true;
  return status;
}

static void io_bench_server_interrupt(io_bench_server *server) {
  if (!io_bench_socket_valid(server->socket_value)) return;
#ifdef _WIN32
  (void)shutdown(server->socket_value, SD_BOTH);
#else
  (void)shutdown(server->socket_value, SHUT_RDWR);
#endif
  (void)io_bench_socket_close(server->socket_value);
  server->socket_closed = true;
}

static int io_bench_server_finish(io_bench_server *server) {
  int status = TURBO_OK;
  if (server->thread_started) {
    status = turbo_thread_join(&server->thread);
    turbo_thread_destroy(&server->thread);
    server->thread_started = false;
    if (status == TURBO_OK) status = atomic_load_explicit(&server->status, memory_order_acquire);
  }
  {
    const int close_status =
        server->socket_closed ? TURBO_OK : io_bench_socket_close(server->socket_value);
    server->socket_value = IO_BENCH_INVALID_SOCKET;
    server->socket_closed = true;
    if (status == TURBO_OK) status = close_status;
  }
  return status;
}

static native_io_backend_kind io_bench_backend_kind(void) {
#ifdef _WIN32
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
  return NATIVE_IO_BACKEND_EPOLL;
#else
  return NATIVE_IO_BACKEND_KQUEUE;
#endif
}

static const char *io_bench_backend_name(void) {
#ifdef _WIN32
  return "IOCP";
#elif defined(__linux__)
  return "epoll";
#else
  return "kqueue";
#endif
}

static int io_bench_connect_socket(io_bench_socket *out_socket, io_bench_protocol protocol,
                                   const struct sockaddr_in *address) {
  const int type = protocol == IO_BENCH_TCP ? SOCK_STREAM : SOCK_DGRAM;
  const int socket_protocol = protocol == IO_BENCH_TCP ? IPPROTO_TCP : IPPROTO_UDP;
  const int no_delay = 1;
  io_bench_socket value = socket(AF_INET, type, socket_protocol);
  int status = TURBO_OK;
  if (!io_bench_socket_valid(value)) return io_bench_socket_error();
  if (connect(value, (const struct sockaddr *)address, sizeof(*address)) != 0)
    status = io_bench_socket_error();
  if (status == TURBO_OK && protocol == IO_BENCH_TCP)
    status = io_bench_set_option(value, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay));
  if (status == TURBO_OK) status = io_bench_disable_sigpipe(value);
  if (status != TURBO_OK) (void)io_bench_socket_close(value);
  else *out_socket = value;
  return status;
}

static int io_bench_native_init(io_bench_native *fixture, io_bench_protocol protocol,
                                const struct sockaddr_in *address) {
  const native_io_backend_config config = {io_bench_backend_kind(), 1u, 4u,
                                           IO_BENCH_COMPLETION_CAPACITY};
  int status;
  memset(fixture, 0, sizeof(*fixture));
  fixture->protocol = protocol;
  fixture->socket_value = IO_BENCH_INVALID_SOCKET;
  status = native_io_backend_init(&fixture->backend, &config);
  if (status == TURBO_OK)
    status = io_bench_connect_socket(&fixture->socket_value, protocol, address);
  if (status == TURBO_OK) status = io_bench_set_nonblocking(fixture->socket_value);
  if (status == TURBO_OK)
    status = native_io_backend_attach_socket(&fixture->backend, (uintptr_t)fixture->socket_value,
                                             &fixture->endpoint);
  return status;
}

static int io_bench_native_exchange(io_bench_native *fixture, const unsigned char *sent,
                                    unsigned char *received, size_t length) {
  size_t sent_offset = 0u;
  size_t received_offset = 0u;
  bool send_pending = false;
  bool receive_pending = false;
  while (sent_offset < length || received_offset < length) {
    native_io_completion events[IO_BENCH_COMPLETION_CAPACITY];
    size_t count = 0u;
    int status;
    if (!receive_pending && received_offset < length) {
      native_io_operation operation = {.kind = fixture->protocol == IO_BENCH_TCP
                                                   ? NATIVE_IO_OPERATION_TCP_RECV
                                                   : NATIVE_IO_OPERATION_UDP_RECV_FROM,
                                       .endpoint = fixture->endpoint,
                                       .buffer = received + received_offset,
                                       .length = length - received_offset,
                                       .user_data = 1u};
      native_io_request request;
      status = native_io_backend_submit(&fixture->backend, &operation, &request);
      if (status != TURBO_OK) return status;
      receive_pending = true;
    }
    if (!send_pending && sent_offset < length) {
      native_io_operation operation = {.kind = fixture->protocol == IO_BENCH_TCP
                                                   ? NATIVE_IO_OPERATION_TCP_SEND
                                                   : NATIVE_IO_OPERATION_UDP_SEND_TO,
                                       .endpoint = fixture->endpoint,
                                       .buffer = (void *)(sent + sent_offset),
                                       .length = length - sent_offset,
                                       .user_data = 2u};
      native_io_request request;
      status = native_io_backend_submit(&fixture->backend, &operation, &request);
      if (status != TURBO_OK) return status;
      send_pending = true;
    }
    status = native_io_backend_observe(&fixture->backend, events, IO_BENCH_COMPLETION_CAPACITY,
                                       IO_BENCH_TIMEOUT_MS, &count);
    if (status != TURBO_OK) return status;
    for (size_t index = 0u; index < count; ++index) {
      if (events[index].kind != NATIVE_IO_COMPLETION_OK || events[index].bytes == 0u)
        return events[index].status == TURBO_OK ? TURBO_EIO : events[index].status;
      if (events[index].user_data == 1u) {
        received_offset += events[index].bytes;
        receive_pending = false;
      } else if (events[index].user_data == 2u) {
        sent_offset += events[index].bytes;
        send_pending = false;
      } else return TURBO_EPROTO;
    }
  }
  return memcmp(sent, received, length) == 0 ? TURBO_OK : TURBO_EIO;
}

static void io_bench_native_coroutine_operation_entry(native_io_coroutine *coroutine,
                                                      void *user_data) {
  io_bench_native_coroutine_operation *state = (io_bench_native_coroutine_operation *)user_data;
  while (state->status == TURBO_OK && state->offset < state->length) {
    native_io_completion completion = {0};
    native_io_operation operation = {
        .kind = state->fixture->protocol == IO_BENCH_TCP
                    ? (state->send ? NATIVE_IO_OPERATION_TCP_SEND : NATIVE_IO_OPERATION_TCP_RECV)
                    : (state->send ? NATIVE_IO_OPERATION_UDP_SEND_TO
                                   : NATIVE_IO_OPERATION_UDP_RECV_FROM),
        .endpoint = state->fixture->endpoint,
        .buffer = state->buffer + state->offset,
        .length = state->length - state->offset};
    state->status = native_io_coroutine_await(coroutine, &operation, &completion);
    if (state->status != TURBO_OK) break;
    if (completion.kind != NATIVE_IO_COMPLETION_OK || completion.bytes == 0u ||
        completion.bytes > state->length - state->offset) {
      state->status = completion.status == TURBO_OK ? TURBO_EIO : completion.status;
      break;
    }
    state->offset += completion.bytes;
  }
  state->done = true;
}

static int io_bench_native_coroutine_cancel_and_drain(io_bench_native *fixture,
                                                      native_io_coroutine_task task,
                                                      io_bench_native_coroutine_operation *state) {
  native_io_completion events[IO_BENCH_COMPLETION_CAPACITY];
  int status;
  if (state->done || !native_io_coroutine_task_valid(task)) return TURBO_OK;
  status = native_io_backend_cancel_coroutine(&fixture->backend, task);
  if (status != TURBO_OK && status != TURBO_EALREADY) return status;
  while (!state->done) {
    size_t count = 0u;
    status = native_io_backend_observe(&fixture->backend, events, IO_BENCH_COMPLETION_CAPACITY,
                                       IO_BENCH_TIMEOUT_MS, &count);
    if (status != TURBO_OK) return status;
    if (count != 0u) return TURBO_EPROTO;
  }
  return TURBO_OK;
}

static int io_bench_native_coroutine_exchange(io_bench_native *fixture, const unsigned char *sent,
                                              unsigned char *received, size_t length) {
  io_bench_native_coroutine_operation receive = {fixture,  received, length, 0u,
                                                 TURBO_OK, false,    false};
  io_bench_native_coroutine_operation send = {
      fixture, (unsigned char *)sent, length, 0u, TURBO_OK, true, false};
  native_io_coroutine_task receive_task = {0};
  native_io_coroutine_task send_task = {0};
  native_io_completion events[IO_BENCH_COMPLETION_CAPACITY];
  int status = native_io_backend_spawn_coroutine(
      &fixture->backend, io_bench_native_coroutine_operation_entry, &receive, &receive_task);
  if (status == TURBO_OK)
    status = native_io_backend_spawn_coroutine(
        &fixture->backend, io_bench_native_coroutine_operation_entry, &send, &send_task);
  while (status == TURBO_OK && (!receive.done || !send.done)) {
    size_t count = 0u;
    status = native_io_backend_observe(&fixture->backend, events, IO_BENCH_COMPLETION_CAPACITY,
                                       IO_BENCH_TIMEOUT_MS, &count);
    if (status == TURBO_OK && count != 0u) status = TURBO_EPROTO;
  }
  if (status != TURBO_OK) {
    const int failure = status;
    const int send_drain_status =
        io_bench_native_coroutine_cancel_and_drain(fixture, send_task, &send);
    const int receive_drain_status =
        io_bench_native_coroutine_cancel_and_drain(fixture, receive_task, &receive);
    if (send_drain_status != TURBO_OK) return send_drain_status;
    if (receive_drain_status != TURBO_OK) return receive_drain_status;
    return failure;
  }
  if (send.status != TURBO_OK) return send.status;
  if (receive.status != TURBO_OK) return receive.status;
  return memcmp(sent, received, length) == 0 ? TURBO_OK : TURBO_EIO;
}

static int io_bench_native_destroy(io_bench_native *fixture) {
  int status = TURBO_OK;
  if (native_io_endpoint_valid(fixture->endpoint))
    status = native_io_backend_release_socket(&fixture->backend, fixture->endpoint);
  fixture->endpoint = (native_io_endpoint){0};
  {
    const int close_status = io_bench_socket_close(fixture->socket_value);
    fixture->socket_value = IO_BENCH_INVALID_SOCKET;
    if (status == TURBO_OK) status = close_status;
  }
  if (fixture->backend.impl != NULL) {
    const int close_status = native_io_backend_close(&fixture->backend);
    const int destroy_status =
        close_status == TURBO_OK ? native_io_backend_destroy(&fixture->backend) : close_status;
    if (status == TURBO_OK) status = destroy_status;
  }
  return status;
}

static void io_bench_libuv_fail(io_bench_libuv *fixture, int status) {
  if (fixture->status == TURBO_OK) fixture->status = status == 0 ? TURBO_EIO : status;
  fixture->done = true;
}

static void io_bench_libuv_alloc(uv_handle_t *handle, size_t suggested, uv_buf_t *buffer) {
  io_bench_libuv *fixture = (io_bench_libuv *)handle->data;
  const size_t remaining =
      fixture->received < fixture->payload_size ? fixture->payload_size - fixture->received : 0u;
  (void)suggested;
  buffer->base = remaining == 0u ? (char *)&fixture->overflow
                                 : (char *)(fixture->received_data + fixture->received);
  buffer->len = remaining == 0u ? 1u : remaining;
}

static void io_bench_libuv_try_done(io_bench_libuv *fixture) {
  if (fixture->received == fixture->payload_size && !fixture->write_pending) fixture->done = true;
}

static void io_bench_libuv_tcp_written(uv_write_t *request, int status) {
  io_bench_libuv *fixture = (io_bench_libuv *)request->data;
  fixture->write_pending = false;
  if (status < 0) io_bench_libuv_fail(fixture, status);
  else io_bench_libuv_try_done(fixture);
}

static void io_bench_libuv_udp_written(uv_udp_send_t *request, int status) {
  io_bench_libuv *fixture = (io_bench_libuv *)request->data;
  fixture->write_pending = false;
  if (status < 0) io_bench_libuv_fail(fixture, status);
  else io_bench_libuv_try_done(fixture);
}

static void io_bench_libuv_tcp_read(uv_stream_t *stream, ssize_t size, const uv_buf_t *buffer) {
  io_bench_libuv *fixture = (io_bench_libuv *)stream->data;
  (void)buffer;
  if (size <= 0) {
    if (size < 0) io_bench_libuv_fail(fixture, (int)size);
    return;
  }
  if ((size_t)size > fixture->payload_size - fixture->received) {
    io_bench_libuv_fail(fixture, TURBO_EIO);
    return;
  }
  fixture->received += (size_t)size;
  if (fixture->received == fixture->payload_size) {
    const int status = uv_read_stop(stream);
    fixture->read_active = false;
    if (status < 0) io_bench_libuv_fail(fixture, status);
    else io_bench_libuv_try_done(fixture);
  }
}

static void io_bench_libuv_udp_read(uv_udp_t *handle, ssize_t size, const uv_buf_t *buffer,
                                    const struct sockaddr *address, unsigned flags) {
  io_bench_libuv *fixture = (io_bench_libuv *)handle->data;
  (void)buffer;
  (void)address;
  if (size < 0 || (flags & UV_UDP_PARTIAL) != 0u || (size_t)size != fixture->payload_size) {
    io_bench_libuv_fail(fixture, size < 0 ? (int)size : TURBO_EIO);
    return;
  }
  fixture->received = (size_t)size;
  {
    const int status = uv_udp_recv_stop(handle);
    fixture->read_active = false;
    if (status < 0) io_bench_libuv_fail(fixture, status);
    else io_bench_libuv_try_done(fixture);
  }
}

static int io_bench_libuv_init(io_bench_libuv *fixture, io_bench_protocol protocol,
                               const struct sockaddr_in *address) {
  io_bench_socket socket_value = IO_BENCH_INVALID_SOCKET;
  int status;
  memset(fixture, 0, sizeof(*fixture));
  fixture->protocol = protocol;
  status = io_bench_connect_socket(&socket_value, protocol, address);
  if (status != TURBO_OK) return status;
  status = uv_loop_init(&fixture->loop);
  if (status < 0) {
    (void)io_bench_socket_close(socket_value);
    return status;
  }
  fixture->loop_initialized = true;
  if (protocol == IO_BENCH_TCP) {
    status = uv_tcp_init(&fixture->loop, &fixture->tcp);
    if (status == 0) {
      fixture->tcp.data = fixture;
      fixture->handle_initialized = true;
      status = uv_tcp_open(&fixture->tcp, (uv_os_sock_t)socket_value);
    }
  } else {
    status = uv_udp_init(&fixture->loop, &fixture->udp);
    if (status == 0) {
      fixture->udp.data = fixture;
      fixture->handle_initialized = true;
      status = uv_udp_open(&fixture->udp, (uv_os_sock_t)socket_value);
    }
  }
  if (status == 0) socket_value = IO_BENCH_INVALID_SOCKET;
  if (io_bench_socket_valid(socket_value)) (void)io_bench_socket_close(socket_value);
  return status < 0 ? status : TURBO_OK;
}

static int io_bench_libuv_exchange(io_bench_libuv *fixture, const unsigned char *sent,
                                   unsigned char *received, size_t length) {
  uv_buf_t buffer = uv_buf_init((char *)sent, (unsigned int)length);
  int status;
  fixture->received_data = received;
  fixture->payload_size = length;
  fixture->received = 0u;
  fixture->status = TURBO_OK;
  fixture->done = false;
  fixture->write_pending = true;
  if (fixture->protocol == IO_BENCH_TCP) {
    status =
        uv_read_start((uv_stream_t *)&fixture->tcp, io_bench_libuv_alloc, io_bench_libuv_tcp_read);
    if (status == 0) {
      fixture->read_active = true;
      fixture->tcp_write.data = fixture;
      status = uv_write(&fixture->tcp_write, (uv_stream_t *)&fixture->tcp, &buffer, 1u,
                        io_bench_libuv_tcp_written);
    }
  } else {
    status = uv_udp_recv_start(&fixture->udp, io_bench_libuv_alloc, io_bench_libuv_udp_read);
    if (status == 0) {
      fixture->read_active = true;
      fixture->udp_write.data = fixture;
      status = uv_udp_send(&fixture->udp_write, &fixture->udp, &buffer, 1u, NULL,
                           io_bench_libuv_udp_written);
    }
  }
  if (status < 0) return status;
  while (!fixture->done) {
    if (uv_run(&fixture->loop, UV_RUN_ONCE) == 0 && !fixture->done) return TURBO_EIO;
  }
  if (fixture->status != TURBO_OK) return fixture->status;
  return memcmp(sent, received, length) == 0 ? TURBO_OK : TURBO_EIO;
}

static int io_bench_libuv_destroy(io_bench_libuv *fixture) {
  int status = TURBO_OK;
  if (fixture->read_active) {
    const int stop_status = fixture->protocol == IO_BENCH_TCP
                                ? uv_read_stop((uv_stream_t *)&fixture->tcp)
                                : uv_udp_recv_stop(&fixture->udp);
    if (stop_status < 0) status = stop_status;
  }
  if (fixture->handle_initialized) {
    uv_handle_t *handle = fixture->protocol == IO_BENCH_TCP ? (uv_handle_t *)&fixture->tcp
                                                            : (uv_handle_t *)&fixture->udp;
    if (!uv_is_closing(handle)) uv_close(handle, NULL);
    (void)uv_run(&fixture->loop, UV_RUN_DEFAULT);
  }
  if (fixture->loop_initialized) {
    const int close_status = uv_loop_close(&fixture->loop);
    if (status == TURBO_OK && close_status < 0) status = close_status;
  }
  return status;
}

static int io_bench_wait_cnet(io_bench_cnet *fixture, const int *value, int expected) {
  while (*value != expected) {
    size_t events = 0u;
    const uint64_t started = fixture->measuring ? turbo_hrtime() : 0u;
    int status;
    if (fixture->status != TURBO_OK) return fixture->status;
    status = cnet_client_poll(&fixture->client, IO_BENCH_TIMEOUT_MS, &events);
    if (fixture->measuring) {
      fixture->poll_ns += turbo_hrtime() - started;
      ++fixture->poll_calls;
    }
    if (status != TURBO_OK) return status;
    if (fixture->status != TURBO_OK) return fixture->status;
    if (*value != expected && events == 0u) return TURBO_ETIMEDOUT;
  }
  return TURBO_OK;
}

static void io_bench_cnet_state(void *user, cnet_connection connection, cnet_connection_state state,
                                const cnet_error *error) {
  io_bench_cnet *fixture = (io_bench_cnet *)user;
  (void)connection;
  if (state == CNET_CONNECTION_CONNECTED) fixture->connected = 1;
  else if (state == CNET_CONNECTION_FAILED || state == CNET_CONNECTION_CLOSED) {
    if (state == CNET_CONNECTION_FAILED) {
      fprintf(stderr, "CNet connection failed: protocol=%s status=%d native_status=%d stage=%s\n",
              fixture->protocol == IO_BENCH_TCP ? "TCP" : "UDP",
              error == NULL ? TURBO_EIO : error->status, error == NULL ? 0 : error->native_status,
              error == NULL || error->stage == NULL ? "unknown" : error->stage);
      fixture->status = error == NULL ? TURBO_EIO : error->status;
    }
    fixture->done = 1;
    fixture->terminal = 1;
  }
}

static void io_bench_cnet_receive(void *user, cnet_connection connection,
                                  const cnet_receive_view *view) {
  io_bench_cnet *fixture = (io_bench_cnet *)user;
  const uint64_t callback_started = fixture->measuring ? turbo_hrtime() : 0u;
  const cnet_message_kind expected =
      fixture->protocol == IO_BENCH_TCP ? CNET_MESSAGE_BYTES : CNET_MESSAGE_DATAGRAM;
  if (view->kind != expected || view->size > fixture->payload_size - fixture->received ||
      (fixture->protocol == IO_BENCH_UDP && view->size != fixture->payload_size)) {
    fprintf(stderr,
            "CNet receive contract mismatch: protocol=%s kind=%d expected=%d size=%zu "
            "remaining=%zu payload=%zu\n",
            fixture->protocol == IO_BENCH_TCP ? "TCP" : "UDP", (int)view->kind, (int)expected,
            view->size, fixture->payload_size - fixture->received, fixture->payload_size);
    fixture->status = TURBO_EIO;
    fixture->done = 1;
    if (fixture->measuring) {
      fixture->callback_ns += turbo_hrtime() - callback_started;
      ++fixture->callback_calls;
    }
    return;
  }
  {
    const uint64_t validation_started = fixture->measuring ? turbo_hrtime() : 0u;
    const int payload_matches =
        memcmp(fixture->expected_data + fixture->received, view->data, view->size) == 0;
    if (fixture->measuring) fixture->payload_validation_ns += turbo_hrtime() - validation_started;
    if (!payload_matches) {
      const unsigned char *received = (const unsigned char *)view->data;
      size_t mismatch = 0u;
      while (mismatch < view->size &&
             fixture->expected_data[fixture->received + mismatch] == received[mismatch])
        ++mismatch;
      fprintf(stderr,
              "CNet payload mismatch: protocol=%s length=%zu offset=%zu expected=%u received=%u\n",
              fixture->protocol == IO_BENCH_TCP ? "TCP" : "UDP", fixture->payload_size,
              fixture->received + mismatch,
              mismatch < view->size
                  ? (unsigned int)fixture->expected_data[fixture->received + mismatch]
                  : 0u,
              mismatch < view->size ? (unsigned int)received[mismatch] : 0u);
      fixture->status = TURBO_EIO;
      fixture->done = 1;
      if (fixture->measuring) {
        fixture->callback_ns += turbo_hrtime() - callback_started;
        ++fixture->callback_calls;
      }
      return;
    }
  }
  fixture->received += view->size;
  if (fixture->received == fixture->payload_size) fixture->done = 1;
  else {
    const uint64_t admission_started = fixture->measuring ? turbo_hrtime() : 0u;
    const int status = cnet_receive(&fixture->client, connection, 1u);
    if (fixture->measuring) {
      fixture->receive_admission_ns += turbo_hrtime() - admission_started;
      ++fixture->receive_admission_calls;
    }
    if (status != TURBO_OK) {
      fixture->status = status;
      fixture->done = 1;
    }
  }
  if (fixture->measuring) {
    fixture->callback_ns += turbo_hrtime() - callback_started;
    ++fixture->callback_calls;
  }
}

static void io_bench_cnet_begin_measurement(io_bench_cnet *fixture) {
  fixture->receive_admission_ns = 0u;
  fixture->send_admission_ns = 0u;
  fixture->poll_ns = 0u;
  fixture->callback_ns = 0u;
  fixture->payload_validation_ns = 0u;
  fixture->receive_admission_calls = 0u;
  fixture->send_admission_calls = 0u;
  fixture->poll_calls = 0u;
  fixture->callback_calls = 0u;
  fixture->measuring = true;
}

static int io_bench_cnet_init(io_bench_cnet *fixture, io_bench_protocol protocol,
                              const struct sockaddr_in *address) {
  const cnet_client_config config = {.backend = io_bench_backend_kind(),
                                     .connection_capacity = 1u,
                                     .command_capacity = 8u,
                                     .request_capacity = 4u,
                                     .completion_batch_capacity = 4u,
                                     .event_capacity = 8u,
                                     .max_send_bytes = IO_BENCH_MAX_PAYLOAD,
                                     .receive_buffer_bytes = IO_BENCH_MAX_PAYLOAD,
                                     .connect_timeout_ms = IO_BENCH_TIMEOUT_MS,
                                     .read_timeout_ms = 0u,
                                     .write_timeout_ms = 0u};
  cnet_connect_options options;
  char uri[64];
  int status;
  memset(fixture, 0, sizeof(*fixture));
  fixture->protocol = protocol;
  fixture->status = TURBO_OK;
  status = cnet_client_init(&fixture->client, &config);
  if (status != TURBO_OK) return status;
  (void)snprintf(uri, sizeof(uri), "%s://127.0.0.1:%u", protocol == IO_BENCH_TCP ? "tcp" : "udp",
                 (unsigned int)ntohs(address->sin_port));
  options = (cnet_connect_options){.uri = uri,
                                   .observer = {.on_state = io_bench_cnet_state,
                                                .on_receive = io_bench_cnet_receive,
                                                .user = fixture}};
  return cnet_connect(&fixture->client, &options, &fixture->connection);
}

static int io_bench_cnet_ready(io_bench_cnet *fixture, size_t payload_size) {
  size_t receive_demand;
  int status = io_bench_wait_cnet(fixture, &fixture->connected, 1);
  if (status == TURBO_OK) status = fixture->status;
  if (status != TURBO_OK) return status;
  if (fixture->protocol == IO_BENCH_TCP) {
    if (payload_size > SIZE_MAX / IO_BENCH_ALL_EXCHANGES) return TURBO_ERANGE;
    receive_demand = payload_size * IO_BENCH_ALL_EXCHANGES;
  } else {
    receive_demand = IO_BENCH_ALL_EXCHANGES;
  }
  status = cnet_receive(&fixture->client, fixture->connection, receive_demand);
  return status;
}

static int io_bench_cnet_exchange(io_bench_cnet *fixture, const unsigned char *sent,
                                  unsigned char *received, size_t length) {
  int status;
  (void)received;
  fixture->expected_data = sent;
  fixture->payload_size = length;
  fixture->received = 0u;
  fixture->done = 0;
  fixture->status = TURBO_OK;
  status = TURBO_OK;
  if (status == TURBO_OK) {
    const uint64_t started = fixture->measuring ? turbo_hrtime() : 0u;
    status = cnet_send(&fixture->client, fixture->connection, sent, length);
    if (fixture->measuring) {
      fixture->send_admission_ns += turbo_hrtime() - started;
      ++fixture->send_admission_calls;
    }
  }
  if (status == TURBO_OK) status = io_bench_wait_cnet(fixture, &fixture->done, 1);
  if (status == TURBO_OK) status = fixture->status;
  return status;
}

static int io_bench_cnet_destroy(io_bench_cnet *fixture) {
  int status = TURBO_OK;
  if (fixture->client.impl != NULL) {
    status = cnet_close(&fixture->client, fixture->connection);
    if (status == TURBO_OK) status = io_bench_wait_cnet(fixture, &fixture->terminal, 1);
    if (status == TURBO_OK || status == TURBO_EALREADY || status == TURBO_ENOENT)
      status = cnet_client_stop(&fixture->client, IO_BENCH_TIMEOUT_MS);
    if (status == TURBO_OK) status = cnet_client_destroy(&fixture->client);
  }
  return status;
}

static int io_bench_fixture_init(io_bench_fixture *fixture, io_bench_protocol protocol,
                                 io_bench_driver driver, size_t payload_size) {
  int status;
  memset(fixture, 0, sizeof(*fixture));
  fixture->driver = driver;
  fixture->protocol = protocol;
  fixture->server.socket_value = IO_BENCH_INVALID_SOCKET;
  fixture->native.socket_value = IO_BENCH_INVALID_SOCKET;
  status = io_bench_network_start(fixture);
  if (status == TURBO_OK) status = io_bench_server_init(&fixture->server, protocol, payload_size);
  if (status == TURBO_OK) status = io_bench_server_start(&fixture->server);
  if (status == TURBO_OK) {
    if (driver == IO_BENCH_LIBUV)
      status = io_bench_libuv_init(&fixture->libuv, protocol, &fixture->server.address);
    else if (driver == IO_BENCH_NATIVE_IO || driver == IO_BENCH_NATIVE_IO_COROUTINE)
      status = io_bench_native_init(&fixture->native, protocol, &fixture->server.address);
    else status = io_bench_cnet_init(&fixture->cnet, protocol, &fixture->server.address);
  }
  if (status == TURBO_OK && driver == IO_BENCH_CNET)
    status = io_bench_cnet_ready(&fixture->cnet, payload_size);
  return status;
}

static int io_bench_exchange(io_bench_fixture *fixture, const unsigned char *sent,
                             unsigned char *received, size_t length) {
  if (fixture->driver == IO_BENCH_LIBUV)
    return io_bench_libuv_exchange(&fixture->libuv, sent, received, length);
  if (fixture->driver == IO_BENCH_NATIVE_IO)
    return io_bench_native_exchange(&fixture->native, sent, received, length);
  if (fixture->driver == IO_BENCH_NATIVE_IO_COROUTINE)
    return io_bench_native_coroutine_exchange(&fixture->native, sent, received, length);
  return io_bench_cnet_exchange(&fixture->cnet, sent, received, length);
}

static int io_bench_fixture_destroy(io_bench_fixture *fixture, bool abort_server) {
  int status;
  int server_status;
  if (fixture->driver == IO_BENCH_LIBUV) status = io_bench_libuv_destroy(&fixture->libuv);
  else if (fixture->driver == IO_BENCH_NATIVE_IO || fixture->driver == IO_BENCH_NATIVE_IO_COROUTINE)
    status = io_bench_native_destroy(&fixture->native);
  else status = io_bench_cnet_destroy(&fixture->cnet);
  if (abort_server) io_bench_server_interrupt(&fixture->server);
  server_status = io_bench_server_finish(&fixture->server);
  if (status == TURBO_OK) status = server_status;
  io_bench_network_stop(fixture);
  return status;
}

static int io_bench_u64_compare(const void *left, const void *right) {
  const uint64_t lhs = *(const uint64_t *)left;
  const uint64_t rhs = *(const uint64_t *)right;
  return lhs < rhs ? -1 : lhs > rhs;
}

static const char *io_bench_driver_name(io_bench_driver driver) {
  if (driver == IO_BENCH_LIBUV) return "libuv";
  if (driver == IO_BENCH_NATIVE_IO) return "NativeIO direct";
  return driver == IO_BENCH_NATIVE_IO_COROUTINE ? "NativeIO coroutine" : "CNet";
}

static int io_bench_run(io_bench_protocol protocol, io_bench_driver driver, size_t payload_size,
                        bool profile_cnet_stages, io_bench_result *result) {
  io_bench_fixture fixture;
  unsigned char *sent = NULL;
  unsigned char *received = NULL;
  uint64_t *latencies = NULL;
  uint64_t wall_started = 0u;
  size_t latency_count = 0u;
  const char *phase = "init";
  int status;
  memset(result, 0, sizeof(*result));
  status = io_bench_fixture_init(&fixture, protocol, driver, payload_size);
  if (status != TURBO_OK) goto cleanup;
  phase = "allocate";
  sent = (unsigned char *)malloc(payload_size);
  received = (unsigned char *)malloc(payload_size);
  latencies = (uint64_t *)malloc(sizeof(*latencies) * IO_BENCH_TOTAL_EXCHANGES);
  if (sent == NULL || received == NULL || latencies == NULL) {
    status = TURBO_ENOMEM;
    goto cleanup;
  }
  memset(sent, 0x5a, payload_size);
  phase = "warmup";
  for (size_t index = 0u; index < IO_BENCH_WARMUP_EXCHANGES; ++index) {
    status = io_bench_exchange(&fixture, sent, received, payload_size);
    if (status != TURBO_OK) goto cleanup;
  }
  if (driver == IO_BENCH_CNET && profile_cnet_stages)
    io_bench_cnet_begin_measurement(&fixture.cnet);
  phase = "measure";
  wall_started = turbo_hrtime();
  for (size_t exchange = 0u; exchange < IO_BENCH_EXCHANGES_PER_REPLICATE; ++exchange) {
    const uint64_t started = turbo_hrtime();
    status = io_bench_exchange(&fixture, sent, received, payload_size);
    if (status != TURBO_OK) goto cleanup;
    latencies[latency_count++] = turbo_hrtime() - started;
  }
  result->payload_size = payload_size;
  result->round_trips = latency_count;
  result->wall_ns = turbo_hrtime() - wall_started;
  qsort(latencies, latency_count, sizeof(latencies[0]), io_bench_u64_compare);
  result->p50_ns = latencies[(latency_count - 1u) * 50u / 100u];
  result->p95_ns = latencies[(latency_count - 1u) * 95u / 100u];
  if (driver == IO_BENCH_CNET && profile_cnet_stages) {
    result->cnet_receive_admission_ns = fixture.cnet.receive_admission_ns;
    result->cnet_send_admission_ns = fixture.cnet.send_admission_ns;
    result->cnet_poll_ns = fixture.cnet.poll_ns;
    result->cnet_callback_ns = fixture.cnet.callback_ns;
    result->cnet_payload_validation_ns = fixture.cnet.payload_validation_ns;
    result->cnet_receive_admission_calls = fixture.cnet.receive_admission_calls;
    result->cnet_send_admission_calls = fixture.cnet.send_admission_calls;
    result->cnet_poll_calls = fixture.cnet.poll_calls;
    result->cnet_callback_calls = fixture.cnet.callback_calls;
    fixture.cnet.measuring = false;
  }
  status = TURBO_OK;

cleanup:
  free(latencies);
  free(received);
  free(sent);
  {
    const int cleanup_status = io_bench_fixture_destroy(&fixture, status != TURBO_OK);
    if (status == TURBO_OK) status = cleanup_status;
  }
  if (status != TURBO_OK)
    fprintf(stderr, "benchmark driver=%s protocol=%s payload=%zu phase=%s status=%d\n",
            io_bench_driver_name(driver), protocol == IO_BENCH_TCP ? "TCP" : "UDP", payload_size,
            phase, status);
  return status;
}

static double io_bench_rate(const io_bench_result *result) {
  return result->wall_ns == 0u
             ? 0.0
             : (double)result->round_trips * 1000000000.0 / (double)result->wall_ns;
}

static double io_bench_mean(uint64_t total, size_t count) {
  return count == 0u ? 0.0 : (double)total / (double)count;
}

typedef enum io_bench_metric {
  IO_BENCH_METRIC_P50 = 0,
  IO_BENCH_METRIC_P95,
  IO_BENCH_METRIC_RATE
} io_bench_metric;

static double io_bench_metric_value(const io_bench_result *result, io_bench_metric metric) {
  if (metric == IO_BENCH_METRIC_P50) return (double)result->p50_ns;
  if (metric == IO_BENCH_METRIC_P95) return (double)result->p95_ns;
  return io_bench_rate(result);
}

static int io_bench_series_summarize(const io_bench_series *series, io_bench_metric metric,
                                     cnet_benchmark_summary *out_summary) {
  double values[IO_BENCH_REPLICATES];
  for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat)
    values[repeat] = io_bench_metric_value(&series->runs[repeat], metric);
  return cnet_benchmark_summarize(values, IO_BENCH_REPLICATES, out_summary);
}

static int io_bench_series_finalize(io_bench_series *series, io_bench_driver driver) {
  double values[IO_BENCH_REPLICATES];
  int status;

  series->payload_size = series->runs[0].payload_size;
  status = io_bench_series_summarize(series, IO_BENCH_METRIC_P50, &series->p50_ns);
  if (status == TURBO_OK)
    status = io_bench_series_summarize(series, IO_BENCH_METRIC_P95, &series->p95_ns);
  if (status == TURBO_OK)
    status = io_bench_series_summarize(series, IO_BENCH_METRIC_RATE, &series->rate_per_second);
  if (status != TURBO_OK || driver != IO_BENCH_CNET) return status;

  for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
    const io_bench_result *result = &series->cnet_profile_runs[repeat];
    values[repeat] =
        io_bench_mean(result->cnet_send_admission_ns, result->cnet_send_admission_calls);
  }
  status = cnet_benchmark_summarize(values, IO_BENCH_REPLICATES, &series->cnet_send_admission_ns);
  for (size_t repeat = 0u; status == TURBO_OK && repeat < IO_BENCH_REPLICATES; ++repeat) {
    const io_bench_result *result = &series->cnet_profile_runs[repeat];
    values[repeat] = io_bench_mean(result->cnet_poll_ns, result->cnet_poll_calls);
  }
  if (status == TURBO_OK)
    status = cnet_benchmark_summarize(values, IO_BENCH_REPLICATES, &series->cnet_poll_ns);
  for (size_t repeat = 0u; status == TURBO_OK && repeat < IO_BENCH_REPLICATES; ++repeat) {
    const io_bench_result *result = &series->cnet_profile_runs[repeat];
    const double callback_ns = io_bench_mean(result->cnet_callback_ns, result->cnet_callback_calls);
    const double validation_ns =
        io_bench_mean(result->cnet_payload_validation_ns, result->cnet_callback_calls);
    values[repeat] = callback_ns > validation_ns ? callback_ns - validation_ns : 0.0;
  }
  if (status == TURBO_OK)
    status =
        cnet_benchmark_summarize(values, IO_BENCH_REPLICATES, &series->cnet_callback_control_ns);
  for (size_t repeat = 0u; status == TURBO_OK && repeat < IO_BENCH_REPLICATES; ++repeat) {
    const io_bench_result *result = &series->cnet_profile_runs[repeat];
    values[repeat] = io_bench_mean(result->cnet_payload_validation_ns, result->cnet_callback_calls);
  }
  if (status == TURBO_OK)
    status =
        cnet_benchmark_summarize(values, IO_BENCH_REPLICATES, &series->cnet_payload_validation_ns);
  for (size_t repeat = 0u; status == TURBO_OK && repeat < IO_BENCH_REPLICATES; ++repeat) {
    const io_bench_result *result = &series->cnet_profile_runs[repeat];
    values[repeat] = (double)result->cnet_poll_calls / (double)result->round_trips;
  }
  if (status == TURBO_OK)
    status =
        cnet_benchmark_summarize(values, IO_BENCH_REPLICATES, &series->cnet_polls_per_round_trip);
  return status;
}

static int io_bench_paired_delta(const io_bench_series *baseline, const io_bench_series *candidate,
                                 io_bench_metric metric, cnet_benchmark_summary *out_summary) {
  double baseline_values[IO_BENCH_REPLICATES];
  double candidate_values[IO_BENCH_REPLICATES];
  for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
    baseline_values[repeat] = io_bench_metric_value(&baseline->runs[repeat], metric);
    candidate_values[repeat] = io_bench_metric_value(&candidate->runs[repeat], metric);
  }
  return cnet_benchmark_summarize_paired_delta(baseline_values, candidate_values,
                                               IO_BENCH_REPLICATES, out_summary);
}

static int io_bench_print_latency(const char *protocol, const char *percentile,
                                  const io_bench_series *libuv, const io_bench_series *native,
                                  const io_bench_series *coroutine, const io_bench_series *cnet,
                                  size_t count, bool p95) {
  const io_bench_metric metric = p95 ? IO_BENCH_METRIC_P95 : IO_BENCH_METRIC_P50;
  printf("\n%s %s round-trip latency\n", protocol, percentile);
  printf("| payload | NativeIO direct median us | NativeIO coroutine median us | CNet median us | "
         "libuv median us |\n");
  printf("| ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    const cnet_benchmark_summary *baseline = p95 ? &native[index].p95_ns : &native[index].p50_ns;
    const cnet_benchmark_summary *libuv_value = p95 ? &libuv[index].p95_ns : &libuv[index].p50_ns;
    const cnet_benchmark_summary *coroutine_value =
        p95 ? &coroutine[index].p95_ns : &coroutine[index].p50_ns;
    const cnet_benchmark_summary *cnet_value = p95 ? &cnet[index].p95_ns : &cnet[index].p50_ns;
    printf("| %zu KiB | %.3f | %.3f | %.3f | %.3f |\n", libuv[index].payload_size / 1024u,
           baseline->median / 1000.0, coroutine_value->median / 1000.0, cnet_value->median / 1000.0,
           libuv_value->median / 1000.0);
  }
  printf("\n%s %s paired latency delta versus NativeIO direct\n", protocol, percentile);
  printf("| payload | NativeIO coroutine median +/- MAD | CNet median +/- MAD | "
         "libuv median +/- MAD |\n");
  printf("| ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    cnet_benchmark_summary coroutine_delta = {0};
    cnet_benchmark_summary cnet_delta = {0};
    cnet_benchmark_summary libuv_delta = {0};
    int status = io_bench_paired_delta(&native[index], &coroutine[index], metric, &coroutine_delta);
    if (status == TURBO_OK)
      status = io_bench_paired_delta(&native[index], &cnet[index], metric, &cnet_delta);
    if (status == TURBO_OK)
      status = io_bench_paired_delta(&native[index], &libuv[index], metric, &libuv_delta);
    if (status != TURBO_OK) return status;
    printf("| %zu KiB | %+.2f%% +/- %.2fpp | %+.2f%% +/- %.2fpp | %+.2f%% +/- %.2fpp |\n",
           libuv[index].payload_size / 1024u, coroutine_delta.median, coroutine_delta.mad,
           cnet_delta.median, cnet_delta.mad, libuv_delta.median, libuv_delta.mad);
  }
  return TURBO_OK;
}

static int io_bench_print_rate(const char *protocol, const io_bench_series *libuv,
                               const io_bench_series *native, const io_bench_series *coroutine,
                               const io_bench_series *cnet, size_t count) {
  printf("\n%s round trips per second\n", protocol);
  printf("| payload | NativeIO direct median | NativeIO coroutine median | CNet median | "
         "libuv median |\n");
  printf("| ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    printf("| %zu KiB | %.0f | %.0f | %.0f | %.0f |\n", libuv[index].payload_size / 1024u,
           native[index].rate_per_second.median, coroutine[index].rate_per_second.median,
           cnet[index].rate_per_second.median, libuv[index].rate_per_second.median);
  }
  printf("\n%s paired rate delta versus NativeIO direct\n", protocol);
  printf("| payload | NativeIO coroutine median +/- MAD | CNet median +/- MAD | "
         "libuv median +/- MAD |\n");
  printf("| ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    cnet_benchmark_summary coroutine_delta = {0};
    cnet_benchmark_summary cnet_delta = {0};
    cnet_benchmark_summary libuv_delta = {0};
    int status = io_bench_paired_delta(&native[index], &coroutine[index], IO_BENCH_METRIC_RATE,
                                       &coroutine_delta);
    if (status == TURBO_OK)
      status =
          io_bench_paired_delta(&native[index], &cnet[index], IO_BENCH_METRIC_RATE, &cnet_delta);
    if (status == TURBO_OK)
      status =
          io_bench_paired_delta(&native[index], &libuv[index], IO_BENCH_METRIC_RATE, &libuv_delta);
    if (status != TURBO_OK) return status;
    printf("| %zu KiB | %+.2f%% +/- %.2fpp | %+.2f%% +/- %.2fpp | %+.2f%% +/- %.2fpp |\n",
           libuv[index].payload_size / 1024u, coroutine_delta.median, coroutine_delta.mad,
           cnet_delta.median, cnet_delta.mad, libuv_delta.median, libuv_delta.mad);
  }
  return TURBO_OK;
}

static void io_bench_print_cnet_stages(const char *protocol, const io_bench_series *cnet,
                                       size_t count) {
  printf("\n%s CNet public API per-run stage medians and MAD\n", protocol);
  printf("| payload | send admit median ns | MAD ns | poll median us | MAD us | "
         "callback control median ns | MAD ns | payload validation median ns | MAD ns | "
         "polls/RT |\n");
  printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    const io_bench_series *series = &cnet[index];
    printf("| %zu KiB | %.1f | %.1f | %.3f | %.3f | %.1f | %.1f | %.1f | %.1f | %.2f |\n",
           series->payload_size / 1024u, series->cnet_send_admission_ns.median,
           series->cnet_send_admission_ns.mad, series->cnet_poll_ns.median / 1000.0,
           series->cnet_poll_ns.mad / 1000.0, series->cnet_callback_control_ns.median,
           series->cnet_callback_control_ns.mad, series->cnet_payload_validation_ns.median,
           series->cnet_payload_validation_ns.mad, series->cnet_polls_per_round_trip.median);
  }
}

static int io_bench_run_row(io_bench_protocol protocol, size_t payload, size_t row,
                            io_bench_series *libuv, io_bench_series *native,
                            io_bench_series *coroutine, io_bench_series *cnet) {
  io_bench_series *series[] = {libuv, native, coroutine, cnet};
  const io_bench_driver order[][4] = {
      {IO_BENCH_LIBUV, IO_BENCH_NATIVE_IO, IO_BENCH_NATIVE_IO_COROUTINE, IO_BENCH_CNET},
      {IO_BENCH_NATIVE_IO, IO_BENCH_NATIVE_IO_COROUTINE, IO_BENCH_CNET, IO_BENCH_LIBUV},
      {IO_BENCH_NATIVE_IO_COROUTINE, IO_BENCH_CNET, IO_BENCH_LIBUV, IO_BENCH_NATIVE_IO},
      {IO_BENCH_CNET, IO_BENCH_LIBUV, IO_BENCH_NATIVE_IO, IO_BENCH_NATIVE_IO_COROUTINE}};
  for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
    for (size_t index = 0u; index < 4u; ++index) {
      const io_bench_driver driver = order[(row + repeat) % 4u][index];
      const int status =
          io_bench_run(protocol, driver, payload, false, &series[driver]->runs[repeat]);
      if (status != TURBO_OK) return status;
    }
  }
  for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
    const int status =
        io_bench_run(protocol, IO_BENCH_CNET, payload, true, &cnet->cnet_profile_runs[repeat]);
    if (status != TURBO_OK) return status;
  }
  for (size_t driver = 0u; driver < 4u; ++driver) {
    const int status = io_bench_series_finalize(series[driver], (io_bench_driver)driver);
    if (status != TURBO_OK) return status;
  }
  return TURBO_OK;
}

spec("libuv versus NativeIO direct versus NativeIO coroutine versus CNet benchmark") {
  it("compares persistent TCP and UDP clients against one common echo peer") {
    const size_t tcp_count = sizeof(IO_BENCH_TCP_PAYLOADS) / sizeof(IO_BENCH_TCP_PAYLOADS[0]);
    const size_t udp_count = sizeof(IO_BENCH_UDP_PAYLOADS) / sizeof(IO_BENCH_UDP_PAYLOADS[0]);
    io_bench_series libuv_tcp[sizeof(IO_BENCH_TCP_PAYLOADS) / sizeof(IO_BENCH_TCP_PAYLOADS[0])] = {
        0};
    io_bench_series native_tcp[sizeof(IO_BENCH_TCP_PAYLOADS) / sizeof(IO_BENCH_TCP_PAYLOADS[0])] = {
        0};
    io_bench_series
        coroutine_tcp[sizeof(IO_BENCH_TCP_PAYLOADS) / sizeof(IO_BENCH_TCP_PAYLOADS[0])] = {0};
    io_bench_series cnet_tcp[sizeof(IO_BENCH_TCP_PAYLOADS) / sizeof(IO_BENCH_TCP_PAYLOADS[0])] = {
        0};
    io_bench_series libuv_udp[sizeof(IO_BENCH_UDP_PAYLOADS) / sizeof(IO_BENCH_UDP_PAYLOADS[0])] = {
        0};
    io_bench_series native_udp[sizeof(IO_BENCH_UDP_PAYLOADS) / sizeof(IO_BENCH_UDP_PAYLOADS[0])] = {
        0};
    io_bench_series
        coroutine_udp[sizeof(IO_BENCH_UDP_PAYLOADS) / sizeof(IO_BENCH_UDP_PAYLOADS[0])] = {0};
    io_bench_series cnet_udp[sizeof(IO_BENCH_UDP_PAYLOADS) / sizeof(IO_BENCH_UDP_PAYLOADS[0])] = {
        0};

    printf("\nBaseline: NativeIO direct; reference: libuv %s; NativeIO backend: %s; CNet: public "
           "byte API.\n",
           uv_version_string(), io_bench_backend_name());
    printf("Each repeat uses a fresh client and dedicated blocking echo peer; driver order rotates "
           "within every matched quartet.\n");
    printf("CNet publishes one bounded receive demand per repeat and consumes borrowed callback "
           "views without an extra payload copy.\n");
    printf("Per-I/O deadlines are disabled in the comparison; timeout behavior is covered by "
           "contract tests.\n");
    printf(
        "Workload: %d independent repeats; each has %d warmups then %d persistent round trips.\n",
        IO_BENCH_REPLICATES, IO_BENCH_WARMUP_EXCHANGES, IO_BENCH_EXCHANGES_PER_REPLICATE);
    printf("Tables report per-repeat medians. Deltas are paired before median/MAD aggregation; "
           "latency > 0 is slower and rate > 0 is faster.\n");
    printf("CNet stage clocks run in separate diagnostic repeats and do not instrument the "
           "comparison rows.\n");

    for (size_t index = 0u; index < tcp_count; ++index)
      check_equal(io_bench_run_row(IO_BENCH_TCP, IO_BENCH_TCP_PAYLOADS[index], index,
                                   &libuv_tcp[index], &native_tcp[index], &coroutine_tcp[index],
                                   &cnet_tcp[index]),
                  TURBO_OK);
    for (size_t index = 0u; index < udp_count; ++index)
      check_equal(io_bench_run_row(IO_BENCH_UDP, IO_BENCH_UDP_PAYLOADS[index], index,
                                   &libuv_udp[index], &native_udp[index], &coroutine_udp[index],
                                   &cnet_udp[index]),
                  TURBO_OK);

    check_equal(io_bench_print_latency("TCP", "p50", libuv_tcp, native_tcp, coroutine_tcp, cnet_tcp,
                                       tcp_count, false),
                TURBO_OK);
    check_equal(io_bench_print_latency("TCP", "p95", libuv_tcp, native_tcp, coroutine_tcp, cnet_tcp,
                                       tcp_count, true),
                TURBO_OK);
    check_equal(
        io_bench_print_rate("TCP", libuv_tcp, native_tcp, coroutine_tcp, cnet_tcp, tcp_count),
        TURBO_OK);
    io_bench_print_cnet_stages("TCP", cnet_tcp, tcp_count);
    check_equal(io_bench_print_latency("UDP", "p50", libuv_udp, native_udp, coroutine_udp, cnet_udp,
                                       udp_count, false),
                TURBO_OK);
    check_equal(io_bench_print_latency("UDP", "p95", libuv_udp, native_udp, coroutine_udp, cnet_udp,
                                       udp_count, true),
                TURBO_OK);
    check_equal(
        io_bench_print_rate("UDP", libuv_udp, native_udp, coroutine_udp, cnet_udp, udp_count),
        TURBO_OK);
    io_bench_print_cnet_stages("UDP", cnet_udp, udp_count);
  }
}
