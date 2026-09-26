#if !defined(_WIN32) && !defined(_GNU_SOURCE)
  #define _GNU_SOURCE
#endif

#include <cnet/cnet.h>
#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/native_io.h>
#include <salts/thread.h>
#include <salts_fs.h>

#include "cnet_benchmark_stats.h"
#include "cnet_client_internal.h"
#include "cnet_io_benchmark_config.h"
#include "tinytest.h"

#include <uv.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
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
typedef enum io_bench_send_mode {
  IO_BENCH_SEND_BASELINE = 0,
  IO_BENCH_SEND_COPY,
  IO_BENCH_SEND_RETAINED,
  IO_BENCH_SEND_VECTOR_COPY,
  IO_BENCH_SEND_NATIVE_VECTOR,
  IO_BENCH_SEND_NATIVE_FLATTEN
} io_bench_send_mode;
enum { IO_BENCH_PASS_A = 0, IO_BENCH_PASS_DIAGNOSTIC, IO_BENCH_PASS_B, IO_BENCH_PASS_COUNT };

enum {
  IO_BENCH_REPLICATES = 5,
  IO_BENCH_EXCHANGES_PER_REPLICATE = 512,
  IO_BENCH_WARMUP_EXCHANGES = 32,
  IO_BENCH_TOTAL_EXCHANGES = IO_BENCH_EXCHANGES_PER_REPLICATE,
  IO_BENCH_ALL_EXCHANGES = IO_BENCH_WARMUP_EXCHANGES + IO_BENCH_TOTAL_EXCHANGES,
  IO_BENCH_TIMEOUT_MS = 5000,
  IO_BENCH_MAX_PAYLOAD = CNET_IO_BENCHMARK_MAX_PAYLOAD,
  IO_BENCH_COMPLETION_CAPACITY = 4,
  IO_BENCH_CSV_LINE_CAPACITY = 1024
};

static const size_t IO_BENCH_TCP_PAYLOADS[] = {1024u, 4096u, 8192u, 16384u, 32768u, 65536u};
static const size_t IO_BENCH_UDP_PAYLOADS[] = {1024u, 4096u, 8192u};

typedef struct io_bench_sample {
  uint64_t wall_ns;
  uint64_t start_ns;
  uint64_t drive_ns;
  uint64_t check_ns;
  size_t start_calls;
  size_t drive_calls;
} io_bench_sample;

typedef struct io_bench_result {
  size_t payload_size;
  size_t round_trips;
  uint64_t wall_ns;
  uint64_t p50_ns;
  uint64_t p95_ns;
  uint64_t cpu_ns;
  uint64_t cpu_cycles;
  uint64_t voluntary_switches;
  uint64_t involuntary_switches;
  uint64_t payload_check_ns;
  io_bench_sample samples[IO_BENCH_TOTAL_EXCHANGES];
  uint64_t cnet_receive_admission_ns;
  uint64_t cnet_send_admission_ns;
  uint64_t cnet_poll_ns;
  uint64_t cnet_callback_ns;
  uint64_t cnet_benchmark_payload_check_ns;
  size_t cnet_receive_admission_calls;
  size_t cnet_send_admission_calls;
  size_t cnet_poll_calls;
  size_t cnet_callback_calls;
  uint64_t native_start_ns;
  uint64_t native_observe_ns;
  size_t native_start_calls;
  size_t native_observe_calls;
  cnet_client_poll_profile cnet_profile;
} io_bench_result;

typedef struct io_bench_series {
  size_t payload_size;
  io_bench_result runs[IO_BENCH_REPLICATES];
  io_bench_result stage_profile_runs[IO_BENCH_REPLICATES];
  cnet_benchmark_summary p50_ns;
  cnet_benchmark_summary p95_ns;
  cnet_benchmark_summary rate_per_second;
  io_bench_result control_runs[IO_BENCH_REPLICATES];
} io_bench_series;

typedef struct io_bench_server {
  io_bench_protocol protocol;
  io_bench_socket socket_value;
  struct sockaddr_in address;
  size_t payload_size;
  size_t exchange_count;
  salts_thread_t thread;
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
  uint64_t benchmark_payload_check_ns;
  size_t receive_admission_calls;
  size_t send_admission_calls;
  size_t poll_calls;
  size_t callback_calls;
  bool measuring;
  io_bench_send_mode send_mode;
  size_t segment_count;
  mem_buffer_t *send_buffer;
  int send_done;
  size_t send_completions;
} io_bench_cnet;

typedef struct io_bench_fixture {
  io_bench_driver driver;
  io_bench_protocol protocol;
  io_bench_server server;
  io_bench_native native;
  io_bench_libuv libuv;
  io_bench_cnet cnet;
  io_bench_send_mode send_mode;
  size_t segment_count;
  unsigned char *flatten_buffer;
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
  return error == 0 ? SALTS_EIO : -error;
}

static int io_bench_network_start(io_bench_fixture *fixture) {
#ifdef _WIN32
  WSADATA data;
  const int status = WSAStartup(MAKEWORD(2, 2), &data);
  if (status != 0) return -status;
#endif
  fixture->network_started = true;
  return SALTS_OK;
}

static void io_bench_network_stop(io_bench_fixture *fixture) {
#ifdef _WIN32
  if (fixture->network_started) (void)WSACleanup();
#endif
  fixture->network_started = false;
}

static int io_bench_socket_close(io_bench_socket value) {
  if (!io_bench_socket_valid(value)) return SALTS_OK;
#ifdef _WIN32
  return closesocket(value) == 0 ? SALTS_OK : io_bench_socket_error();
#else
  return close(value) == 0 ? SALTS_OK : io_bench_socket_error();
#endif
}

static int io_bench_set_option(io_bench_socket value, int level, int option, const void *data,
                               size_t size) {
#ifdef _WIN32
  if (size > INT_MAX) return SALTS_ERANGE;
  return setsockopt(value, level, option, (const char *)data, (int)size) == 0
             ? SALTS_OK
             : io_bench_socket_error();
#else
  return setsockopt(value, level, option, data, (socklen_t)size) == 0 ? SALTS_OK
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
  return ioctlsocket(value, FIONBIO, &enabled) == 0 ? SALTS_OK : io_bench_socket_error();
#else
  const int flags = fcntl(value, F_GETFL, 0);
  if (flags < 0 || fcntl(value, F_SETFL, flags | O_NONBLOCK) != 0) return io_bench_socket_error();
  return SALTS_OK;
#endif
}

static int io_bench_disable_sigpipe(io_bench_socket value) {
#if defined(SO_NOSIGPIPE)
  const int enabled = 1;
  return io_bench_set_option(value, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#else
  (void)value;
  return SALTS_OK;
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
  return SALTS_OK;
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
  return SALTS_OK;
}

static int io_bench_receive_all(io_bench_socket value, unsigned char *data, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
#ifdef _WIN32
    const int received = recv(value, (char *)data + offset, (int)(size - offset), 0);
#else
    const ssize_t received = recv(value, data + offset, size - offset, 0);
#endif
    if (received <= 0) return received == 0 ? SALTS_EIO : io_bench_socket_error();
    offset += (size_t)received;
  }
  return SALTS_OK;
}

static void io_bench_server_entry(void *argument) {
  io_bench_server *server = (io_bench_server *)argument;
  io_bench_socket active = server->socket_value;
  unsigned char *buffer = (unsigned char *)malloc(server->payload_size);
  int status = buffer == NULL ? SALTS_ENOMEM : SALTS_OK;
  if (status == SALTS_OK && server->protocol == IO_BENCH_TCP) {
    const int no_delay = 1;
    active = accept(server->socket_value, NULL, NULL);
    if (!io_bench_socket_valid(active)) status = io_bench_socket_error();
    if (status == SALTS_OK) status = io_bench_set_timeout(active);
    if (status == SALTS_OK)
      status = io_bench_set_option(active, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay));
  }
  for (size_t index = 0u; status == SALTS_OK && index < server->exchange_count; ++index) {
    if (server->protocol == IO_BENCH_TCP) {
      status = io_bench_receive_all(active, buffer, server->payload_size);
      if (status == SALTS_OK) status = io_bench_send_all(active, buffer, server->payload_size);
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
        status = received < 0 ? io_bench_socket_error() : SALTS_EIO;
      if (status == SALTS_OK) {
#ifdef _WIN32
        const int sent = sendto(active, (const char *)buffer, received, 0,
                                (const struct sockaddr *)&peer, peer_length);
#else
        const ssize_t sent = sendto(active, buffer, (size_t)received, 0,
                                    (const struct sockaddr *)&peer, peer_length);
#endif
        if (sent != received) status = sent < 0 ? io_bench_socket_error() : SALTS_EIO;
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
  if (status == SALTS_OK) status = io_bench_set_timeout(server->socket_value);
  if (status == SALTS_OK) status = io_bench_disable_sigpipe(server->socket_value);
  if (status == SALTS_OK && protocol == IO_BENCH_TCP && listen(server->socket_value, 1) != 0)
    status = io_bench_socket_error();
  atomic_init(&server->status, SALTS_OK);
  return status;
}

static int io_bench_server_start(io_bench_server *server) {
  const int status = salts_thread_create(&server->thread, io_bench_server_entry, server);
  if (status == SALTS_OK) server->thread_started = true;
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
  int status = SALTS_OK;
  if (server->thread_started) {
    status = salts_thread_join(&server->thread);
    salts_thread_destroy(&server->thread);
    server->thread_started = false;
    if (status == SALTS_OK) status = atomic_load_explicit(&server->status, memory_order_acquire);
  }
  {
    const int close_status =
        server->socket_closed ? SALTS_OK : io_bench_socket_close(server->socket_value);
    server->socket_value = IO_BENCH_INVALID_SOCKET;
    server->socket_closed = true;
    if (status == SALTS_OK) status = close_status;
  }
  return status;
}

static int io_bench_connect_socket(io_bench_socket *out_socket, io_bench_protocol protocol,
                                   const struct sockaddr_in *address) {
  const int type = protocol == IO_BENCH_TCP ? SOCK_STREAM : SOCK_DGRAM;
  const int socket_protocol = protocol == IO_BENCH_TCP ? IPPROTO_TCP : IPPROTO_UDP;
  const int no_delay = 1;
  io_bench_socket value = socket(AF_INET, type, socket_protocol);
  int status = SALTS_OK;
  if (!io_bench_socket_valid(value)) return io_bench_socket_error();
  if (connect(value, (const struct sockaddr *)address, sizeof(*address)) != 0)
    status = io_bench_socket_error();
  if (status == SALTS_OK && protocol == IO_BENCH_TCP)
    status = io_bench_set_option(value, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay));
  if (status == SALTS_OK) status = io_bench_disable_sigpipe(value);
  if (status != SALTS_OK) (void)io_bench_socket_close(value);
  else *out_socket = value;
  return status;
}

static int io_bench_native_init(io_bench_native *fixture, io_bench_protocol protocol,
                                const struct sockaddr_in *address,
                                native_io_backend_kind backend_kind) {
  const native_io_backend_config config = {backend_kind, 1u, 4u, IO_BENCH_COMPLETION_CAPACITY};
  int status;
  memset(fixture, 0, sizeof(*fixture));
  fixture->protocol = protocol;
  fixture->socket_value = IO_BENCH_INVALID_SOCKET;
  status = native_io_backend_init(&fixture->backend, &config);
  if (status == SALTS_OK)
    status = io_bench_connect_socket(&fixture->socket_value, protocol, address);
  if (status == SALTS_OK) status = io_bench_set_nonblocking(fixture->socket_value);
  if (status == SALTS_OK)
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
      status = native_io_backend_prepare(&fixture->backend, &operation, &request);
      if (status != SALTS_OK) return status;
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
      status = native_io_backend_prepare(&fixture->backend, &operation, &request);
      if (status != SALTS_OK) return status;
      send_pending = true;
    }
    status = native_io_backend_observe(&fixture->backend, events, IO_BENCH_COMPLETION_CAPACITY,
                                       IO_BENCH_TIMEOUT_MS, &count);
    if (status != SALTS_OK) return status;
    for (size_t index = 0u; index < count; ++index) {
      if (events[index].kind != NATIVE_IO_COMPLETION_OK || events[index].bytes == 0u)
        return events[index].status == SALTS_OK ? SALTS_EIO : events[index].status;
      if (events[index].user_data == 1u) {
        received_offset += events[index].bytes;
        receive_pending = false;
      } else if (events[index].user_data == 2u) {
        sent_offset += events[index].bytes;
        send_pending = false;
      } else return SALTS_EPROTO;
    }
  }
  return memcmp(sent, received, length) == 0 ? SALTS_OK : SALTS_EIO;
}

static size_t io_bench_native_segments(const unsigned char *data, size_t length,
                                       size_t consumed, size_t segment_count,
                                       native_io_buffer_span spans[NATIVE_IO_VECTOR_MAX]) {
  size_t output = 0u;
  size_t start = 0u;
  if (data == NULL || segment_count == 0u || segment_count > NATIVE_IO_VECTOR_MAX ||
      length < segment_count || consumed >= length)
    return 0u;
  for (size_t index = 0u; index < segment_count; ++index) {
    const size_t base = length / segment_count;
    const size_t extra = index < length % segment_count ? 1u : 0u;
    const size_t span_length = base + extra;
    const size_t end = start + span_length;
    if (consumed < end) {
      const size_t local = consumed > start ? consumed - start : 0u;
      spans[output++] =
          (native_io_buffer_span){(void *)(data + start + local), span_length - local};
    }
    start = end;
  }
  return output;
}

static size_t io_bench_cnet_segments(const unsigned char *data, size_t length,
                                     size_t segment_count,
                                     cnet_const_buffer segments[NATIVE_IO_VECTOR_MAX]) {
  size_t start = 0u;
  if (data == NULL || segment_count == 0u || segment_count > NATIVE_IO_VECTOR_MAX ||
      length < segment_count)
    return 0u;
  for (size_t index = 0u; index < segment_count; ++index) {
    const size_t base = length / segment_count;
    const size_t extra = index < length % segment_count ? 1u : 0u;
    const size_t span_length = base + extra;
    segments[index] = (cnet_const_buffer){data + start, span_length};
    start += span_length;
  }
  return segment_count;
}

static int io_bench_native_vector_exchange(io_bench_native *fixture,
                                           const unsigned char *sent,
                                           unsigned char *received, size_t length,
                                           size_t segment_count) {
  size_t sent_offset = 0u;
  size_t received_offset = 0u;
  bool send_pending = false;
  bool receive_pending = false;
  if (fixture->protocol != IO_BENCH_TCP) return SALTS_ENOTSUP;
  while (sent_offset < length || received_offset < length) {
    native_io_completion events[IO_BENCH_COMPLETION_CAPACITY];
    size_t count = 0u;
    int status;
    if (!receive_pending && received_offset < length) {
      native_io_operation operation = {.kind = NATIVE_IO_OPERATION_STREAM_RECV,
                                       .endpoint = fixture->endpoint,
                                       .buffer = received + received_offset,
                                       .length = length - received_offset,
                                       .user_data = 1u};
      native_io_request request = {0};
      status = native_io_backend_prepare(&fixture->backend, &operation, &request);
      if (status != SALTS_OK) return status;
      receive_pending = true;
    }
    if (!send_pending && sent_offset < length) {
      native_io_buffer_span spans[NATIVE_IO_VECTOR_MAX];
      const size_t count_spans =
          io_bench_native_segments(sent, length, sent_offset, segment_count, spans);
      native_io_vector_operation operation = {
          NATIVE_IO_OPERATION_STREAM_SEND, fixture->endpoint, spans, count_spans, 2u};
      native_io_request request = {0};
      if (count_spans == 0u) return SALTS_EPROTO;
      status = native_io_backend_submit_vector(&fixture->backend, &operation, &request);
      if (status != SALTS_OK) return status;
      send_pending = true;
    }
    status = native_io_backend_observe(&fixture->backend, events, IO_BENCH_COMPLETION_CAPACITY,
                                       IO_BENCH_TIMEOUT_MS, &count);
    if (status != SALTS_OK) return status;
    for (size_t index = 0u; index < count; ++index) {
      if (events[index].kind != NATIVE_IO_COMPLETION_OK || events[index].bytes == 0u)
        return events[index].status == SALTS_OK ? SALTS_EIO : events[index].status;
      if (events[index].user_data == 1u) {
        if (events[index].bytes > length - received_offset) return SALTS_EPROTO;
        received_offset += events[index].bytes;
        receive_pending = false;
      } else if (events[index].user_data == 2u) {
        if (events[index].bytes > length - sent_offset) return SALTS_EPROTO;
        sent_offset += events[index].bytes;
        send_pending = false;
      } else {
        return SALTS_EPROTO;
      }
    }
  }
  return memcmp(sent, received, length) == 0 ? SALTS_OK : SALTS_EIO;
}

static int io_bench_native_flatten_exchange(io_bench_native *fixture,
                                            const unsigned char *sent,
                                            unsigned char *received, size_t length,
                                            size_t segment_count,
                                            unsigned char *flatten_buffer) {
  cnet_const_buffer segments[NATIVE_IO_VECTOR_MAX];
  size_t offset = 0u;
  const size_t count = io_bench_cnet_segments(sent, length, segment_count, segments);
  if (flatten_buffer == NULL || count == 0u) return SALTS_EINVAL;
  for (size_t index = 0u; index < count; ++index) {
    memcpy(flatten_buffer + offset, segments[index].data, segments[index].size);
    offset += segments[index].size;
  }
  if (offset != length) return SALTS_EPROTO;
  return io_bench_native_exchange(fixture, flatten_buffer, received, length);
}

static int io_bench_check_profiled(const unsigned char *sent, const unsigned char *received,
                                   size_t length, io_bench_result *result) {
  const uint64_t started = salts_hrtime();
  const int status = memcmp(sent, received, length) == 0 ? SALTS_OK : SALTS_EIO;
  result->payload_check_ns += salts_hrtime() - started;
  return status;
}

static void io_bench_record_stage(uint64_t started, uint64_t *total_ns, size_t *calls) {
  *total_ns += salts_hrtime() - started;
  ++*calls;
}

/* Keep diagnostic clocks out of the comparison path above. */
static int io_bench_native_exchange_profiled(io_bench_native *fixture, const unsigned char *sent,
                                             unsigned char *received, size_t length,
                                             io_bench_result *result) {
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
      const uint64_t started = salts_hrtime();
      status = native_io_backend_prepare(&fixture->backend, &operation, &request);
      io_bench_record_stage(started, &result->native_start_ns, &result->native_start_calls);
      if (status != SALTS_OK) return status;
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
      const uint64_t started = salts_hrtime();
      status = native_io_backend_prepare(&fixture->backend, &operation, &request);
      io_bench_record_stage(started, &result->native_start_ns, &result->native_start_calls);
      if (status != SALTS_OK) return status;
      send_pending = true;
    }
    {
      const uint64_t started = salts_hrtime();
      status = native_io_backend_observe(&fixture->backend, events, IO_BENCH_COMPLETION_CAPACITY,
                                         IO_BENCH_TIMEOUT_MS, &count);
      io_bench_record_stage(started, &result->native_observe_ns, &result->native_observe_calls);
    }
    if (status != SALTS_OK) return status;
    for (size_t index = 0u; index < count; ++index) {
      if (events[index].kind != NATIVE_IO_COMPLETION_OK || events[index].bytes == 0u)
        return events[index].status == SALTS_OK ? SALTS_EIO : events[index].status;
      if (events[index].user_data == 1u) {
        received_offset += events[index].bytes;
        receive_pending = false;
      } else if (events[index].user_data == 2u) {
        sent_offset += events[index].bytes;
        send_pending = false;
      } else return SALTS_EPROTO;
    }
  }
  return io_bench_check_profiled(sent, received, length, result);
}

static void io_bench_native_coroutine_operation_entry(native_io_coroutine *coroutine,
                                                      void *user_data) {
  io_bench_native_coroutine_operation *state = (io_bench_native_coroutine_operation *)user_data;
  while (state->status == SALTS_OK && state->offset < state->length) {
    native_io_completion completion = {0};
    native_io_operation operation = {
        .kind = state->fixture->protocol == IO_BENCH_TCP
                    ? (state->send ? NATIVE_IO_OPERATION_TCP_SEND : NATIVE_IO_OPERATION_TCP_RECV)
                    : (state->send ? NATIVE_IO_OPERATION_UDP_SEND_TO
                                   : NATIVE_IO_OPERATION_UDP_RECV_FROM),
        .endpoint = state->fixture->endpoint,
        .buffer = state->buffer + state->offset,
        .length = state->length - state->offset};
    state->status = native_io_coroutine_await_prepared(coroutine, &operation, &completion);
    if (state->status != SALTS_OK) break;
    if (completion.kind != NATIVE_IO_COMPLETION_OK || completion.bytes == 0u ||
        completion.bytes > state->length - state->offset) {
      state->status = completion.status == SALTS_OK ? SALTS_EIO : completion.status;
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
  if (state->done || !native_io_coroutine_task_valid(task)) return SALTS_OK;
  status = native_io_backend_cancel_coroutine(&fixture->backend, task);
  if (status != SALTS_OK && status != SALTS_EALREADY) return status;
  while (!state->done) {
    size_t count = 0u;
    status = native_io_backend_observe(&fixture->backend, events, IO_BENCH_COMPLETION_CAPACITY,
                                       IO_BENCH_TIMEOUT_MS, &count);
    if (status != SALTS_OK) return status;
    if (count != 0u) return SALTS_EPROTO;
  }
  return SALTS_OK;
}

static int io_bench_native_coroutine_exchange(io_bench_native *fixture, const unsigned char *sent,
                                              unsigned char *received, size_t length) {
  io_bench_native_coroutine_operation receive = {fixture,  received, length, 0u,
                                                 SALTS_OK, false,    false};
  io_bench_native_coroutine_operation send = {
      fixture, (unsigned char *)sent, length, 0u, SALTS_OK, true, false};
  native_io_coroutine_task receive_task = {0};
  native_io_coroutine_task send_task = {0};
  native_io_completion events[IO_BENCH_COMPLETION_CAPACITY];
  int status = native_io_backend_spawn_coroutine(
      &fixture->backend, io_bench_native_coroutine_operation_entry, &receive, &receive_task);
  if (status == SALTS_OK)
    status = native_io_backend_spawn_coroutine(
        &fixture->backend, io_bench_native_coroutine_operation_entry, &send, &send_task);
  while (status == SALTS_OK && (!receive.done || !send.done)) {
    size_t count = 0u;
    status = native_io_backend_observe(&fixture->backend, events, IO_BENCH_COMPLETION_CAPACITY,
                                       IO_BENCH_TIMEOUT_MS, &count);
    if (status == SALTS_OK && count != 0u) status = SALTS_EPROTO;
  }
  if (status != SALTS_OK) {
    const int failure = status;
    const int send_drain_status =
        io_bench_native_coroutine_cancel_and_drain(fixture, send_task, &send);
    const int receive_drain_status =
        io_bench_native_coroutine_cancel_and_drain(fixture, receive_task, &receive);
    if (send_drain_status != SALTS_OK) return send_drain_status;
    if (receive_drain_status != SALTS_OK) return receive_drain_status;
    return failure;
  }
  if (send.status != SALTS_OK) return send.status;
  if (receive.status != SALTS_OK) return receive.status;
  return memcmp(sent, received, length) == 0 ? SALTS_OK : SALTS_EIO;
}

static int io_bench_native_coroutine_exchange_profiled(io_bench_native *fixture,
                                                       const unsigned char *sent,
                                                       unsigned char *received, size_t length,
                                                       io_bench_result *result) {
  io_bench_native_coroutine_operation receive = {fixture,  received, length, 0u,
                                                 SALTS_OK, false,    false};
  io_bench_native_coroutine_operation send = {
      fixture, (unsigned char *)sent, length, 0u, SALTS_OK, true, false};
  native_io_coroutine_task receive_task = {0};
  native_io_coroutine_task send_task = {0};
  native_io_completion events[IO_BENCH_COMPLETION_CAPACITY];
  uint64_t started = salts_hrtime();
  int status = native_io_backend_spawn_coroutine(
      &fixture->backend, io_bench_native_coroutine_operation_entry, &receive, &receive_task);
  io_bench_record_stage(started, &result->native_start_ns, &result->native_start_calls);
  if (status == SALTS_OK) {
    started = salts_hrtime();
    status = native_io_backend_spawn_coroutine(
        &fixture->backend, io_bench_native_coroutine_operation_entry, &send, &send_task);
    io_bench_record_stage(started, &result->native_start_ns, &result->native_start_calls);
  }
  while (status == SALTS_OK && (!receive.done || !send.done)) {
    size_t count = 0u;
    started = salts_hrtime();
    status = native_io_backend_observe(&fixture->backend, events, IO_BENCH_COMPLETION_CAPACITY,
                                       IO_BENCH_TIMEOUT_MS, &count);
    io_bench_record_stage(started, &result->native_observe_ns, &result->native_observe_calls);
    if (status == SALTS_OK && count != 0u) status = SALTS_EPROTO;
  }
  if (status != SALTS_OK) {
    const int failure = status;
    const int send_drain_status =
        io_bench_native_coroutine_cancel_and_drain(fixture, send_task, &send);
    const int receive_drain_status =
        io_bench_native_coroutine_cancel_and_drain(fixture, receive_task, &receive);
    if (send_drain_status != SALTS_OK) return send_drain_status;
    if (receive_drain_status != SALTS_OK) return receive_drain_status;
    return failure;
  }
  if (send.status != SALTS_OK) return send.status;
  if (receive.status != SALTS_OK) return receive.status;
  return io_bench_check_profiled(sent, received, length, result);
}

static int io_bench_native_destroy(io_bench_native *fixture) {
  int status = SALTS_OK;
  if (native_io_endpoint_valid(fixture->endpoint))
    status = native_io_backend_release_socket(&fixture->backend, fixture->endpoint);
  fixture->endpoint = (native_io_endpoint){0};
  {
    const int close_status = io_bench_socket_close(fixture->socket_value);
    fixture->socket_value = IO_BENCH_INVALID_SOCKET;
    if (status == SALTS_OK) status = close_status;
  }
  if (fixture->backend.impl != NULL) {
    const int close_status = native_io_backend_close(&fixture->backend);
    const int destroy_status =
        close_status == SALTS_OK ? native_io_backend_destroy(&fixture->backend) : close_status;
    if (status == SALTS_OK) status = destroy_status;
  }
  return status;
}

static void io_bench_libuv_fail(io_bench_libuv *fixture, int status) {
  if (fixture->status == SALTS_OK) fixture->status = status == 0 ? SALTS_EIO : status;
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
    io_bench_libuv_fail(fixture, SALTS_EIO);
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
    io_bench_libuv_fail(fixture, size < 0 ? (int)size : SALTS_EIO);
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
  if (status != SALTS_OK) return status;
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
  return status < 0 ? status : SALTS_OK;
}

static int io_bench_libuv_exchange(io_bench_libuv *fixture, const unsigned char *sent,
                                   unsigned char *received, size_t length,
                                   io_bench_result *result) {
  uv_buf_t buffer = uv_buf_init((char *)sent, (unsigned int)length);
  int status;
  uint64_t started;
  fixture->received_data = received;
  fixture->payload_size = length;
  fixture->received = 0u;
  fixture->status = SALTS_OK;
  fixture->done = false;
  fixture->write_pending = true;
  started = result != NULL ? salts_hrtime() : 0u;
  if (fixture->protocol == IO_BENCH_TCP) {
    status =
        uv_read_start((uv_stream_t *)&fixture->tcp, io_bench_libuv_alloc, io_bench_libuv_tcp_read);
    if (result != NULL)
      io_bench_record_stage(started, &result->native_start_ns, &result->native_start_calls);
    if (status == 0) {
      fixture->read_active = true;
      fixture->tcp_write.data = fixture;
      started = result != NULL ? salts_hrtime() : 0u;
      status = uv_write(&fixture->tcp_write, (uv_stream_t *)&fixture->tcp, &buffer, 1u,
                        io_bench_libuv_tcp_written);
      if (result != NULL)
        io_bench_record_stage(started, &result->native_start_ns, &result->native_start_calls);
    }
  } else {
    status = uv_udp_recv_start(&fixture->udp, io_bench_libuv_alloc, io_bench_libuv_udp_read);
    if (result != NULL)
      io_bench_record_stage(started, &result->native_start_ns, &result->native_start_calls);
    if (status == 0) {
      fixture->read_active = true;
      fixture->udp_write.data = fixture;
      started = result != NULL ? salts_hrtime() : 0u;
      status = uv_udp_send(&fixture->udp_write, &fixture->udp, &buffer, 1u, NULL,
                           io_bench_libuv_udp_written);
      if (result != NULL)
        io_bench_record_stage(started, &result->native_start_ns, &result->native_start_calls);
    }
  }
  if (status < 0) return status;
  while (!fixture->done) {
    const uint64_t drive_started = result != NULL ? salts_hrtime() : 0u;
    const int alive = uv_run(&fixture->loop, UV_RUN_ONCE);
    if (result != NULL)
      io_bench_record_stage(drive_started, &result->native_observe_ns, &result->native_observe_calls);
    if (alive == 0 && !fixture->done) return SALTS_EIO;
  }
  if (fixture->status != SALTS_OK) return fixture->status;
  if (result != NULL) return io_bench_check_profiled(sent, received, length, result);
  return memcmp(sent, received, length) == 0 ? SALTS_OK : SALTS_EIO;
}

static int io_bench_libuv_destroy(io_bench_libuv *fixture) {
  int status = SALTS_OK;
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
    if (status == SALTS_OK && close_status < 0) status = close_status;
  }
  return status;
}

static int io_bench_wait_cnet(io_bench_cnet *fixture, const int *value, int expected) {
  while (*value != expected) {
    size_t events = 0u;
    const uint64_t started = fixture->measuring ? salts_hrtime() : 0u;
    int status;
    if (fixture->status != SALTS_OK) return fixture->status;
    status = cnet_client_poll(&fixture->client, IO_BENCH_TIMEOUT_MS, &events);
    if (fixture->measuring) {
      fixture->poll_ns += salts_hrtime() - started;
      ++fixture->poll_calls;
    }
    if (status != SALTS_OK) return status;
    if (fixture->status != SALTS_OK) return fixture->status;
    if (*value != expected && events == 0u) return SALTS_ETIMEDOUT;
  }
  return SALTS_OK;
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
              error == NULL ? SALTS_EIO : error->status, error == NULL ? 0 : error->native_status,
              error == NULL || error->stage == NULL ? "unknown" : error->stage);
      fixture->status = error == NULL ? SALTS_EIO : error->status;
    }
    fixture->done = 1;
    fixture->terminal = 1;
  }
}

static void io_bench_cnet_receive(void *user, cnet_connection connection,
                                  const cnet_receive_view *view) {
  io_bench_cnet *fixture = (io_bench_cnet *)user;
  const uint64_t callback_started = fixture->measuring ? salts_hrtime() : 0u;
  const cnet_message_kind expected =
      fixture->protocol == IO_BENCH_TCP ? CNET_MESSAGE_BYTES : CNET_MESSAGE_DATAGRAM;
  if (view->kind != expected || view->size > fixture->payload_size - fixture->received ||
      (fixture->protocol == IO_BENCH_UDP && view->size != fixture->payload_size)) {
    fprintf(stderr,
            "CNet receive contract mismatch: protocol=%s kind=%d expected=%d size=%zu "
            "remaining=%zu payload=%zu\n",
            fixture->protocol == IO_BENCH_TCP ? "TCP" : "UDP", (int)view->kind, (int)expected,
            view->size, fixture->payload_size - fixture->received, fixture->payload_size);
    fixture->status = SALTS_EIO;
    fixture->done = 1;
    if (fixture->measuring) {
      fixture->callback_ns += salts_hrtime() - callback_started;
      ++fixture->callback_calls;
    }
    return;
  }
  {
    const uint64_t payload_check_started = fixture->measuring ? salts_hrtime() : 0u;
    const int payload_matches =
        memcmp(fixture->expected_data + fixture->received, view->data, view->size) == 0;
    if (fixture->measuring) fixture->benchmark_payload_check_ns += salts_hrtime() - payload_check_started;
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
      fixture->status = SALTS_EIO;
      fixture->done = 1;
      if (fixture->measuring) {
        fixture->callback_ns += salts_hrtime() - callback_started;
        ++fixture->callback_calls;
      }
      return;
    }
  }
  fixture->received += view->size;
  if (fixture->received == fixture->payload_size) fixture->done = 1;
  else {
    const uint64_t admission_started = fixture->measuring ? salts_hrtime() : 0u;
    const int status = cnet_receive(&fixture->client, connection, 1u);
    if (fixture->measuring) {
      fixture->receive_admission_ns += salts_hrtime() - admission_started;
      ++fixture->receive_admission_calls;
    }
    if (status != SALTS_OK) {
      fixture->status = status;
      fixture->done = 1;
    }
  }
  if (fixture->measuring) {
    fixture->callback_ns += salts_hrtime() - callback_started;
    ++fixture->callback_calls;
  }
}

static int io_bench_cnet_begin_measurement(io_bench_cnet *fixture) {
  fixture->receive_admission_ns = 0u;
  fixture->send_admission_ns = 0u;
  fixture->poll_ns = 0u;
  fixture->callback_ns = 0u;
  fixture->benchmark_payload_check_ns = 0u;
  fixture->receive_admission_calls = 0u;
  fixture->send_admission_calls = 0u;
  fixture->poll_calls = 0u;
  fixture->callback_calls = 0u;
  {
    const int status = cnet_client_profile_begin(&fixture->client);
    if (status != SALTS_OK) return status;
  }
  fixture->measuring = true;
  return SALTS_OK;
}

static void io_bench_cnet_sent(void *user, cnet_connection connection, size_t size) {
  io_bench_cnet *fixture = (io_bench_cnet *)user;
  (void)connection;
  if (fixture->send_done || size != fixture->payload_size) fixture->status = SALTS_EIO;
  fixture->send_done = 1;
  ++fixture->send_completions;
}

static int io_bench_cnet_init(io_bench_cnet *fixture, io_bench_protocol protocol,
                              const struct sockaddr_in *address,
                              native_io_backend_kind backend_kind, io_bench_send_mode send_mode) {
  const cnet_client_config config = {.backend = backend_kind,
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
  fixture->send_mode = send_mode;
  fixture->status = SALTS_OK;
  status = cnet_client_init(&fixture->client, &config);
  if (status != SALTS_OK) return status;
  (void)snprintf(uri, sizeof(uri), "%s://127.0.0.1:%u", protocol == IO_BENCH_TCP ? "tcp" : "udp",
                 (unsigned int)ntohs(address->sin_port));
  options = (cnet_connect_options){.uri = uri,
                                   .observer = {.on_state = io_bench_cnet_state,
                                                .on_receive = io_bench_cnet_receive,
                                                .on_send = send_mode == IO_BENCH_SEND_BASELINE
                                                               ? NULL : io_bench_cnet_sent,
                                                .user = fixture}};
  return cnet_connect(&fixture->client, &options, &fixture->connection);
}

static int io_bench_cnet_ready(io_bench_cnet *fixture, size_t payload_size) {
  size_t receive_demand;
  int status = io_bench_wait_cnet(fixture, &fixture->connected, 1);
  if (status == SALTS_OK) status = fixture->status;
  if (status != SALTS_OK) return status;
  if (fixture->protocol == IO_BENCH_TCP) {
    if (payload_size > SIZE_MAX / IO_BENCH_ALL_EXCHANGES) return SALTS_ERANGE;
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
  fixture->send_done = 0;
  fixture->status = SALTS_OK;
  status = SALTS_OK;
  if (status == SALTS_OK) {
    const uint64_t started = fixture->measuring ? salts_hrtime() : 0u;
    status = fixture->send_mode == IO_BENCH_SEND_RETAINED
                 ? cnet_send_buffer(&fixture->client, fixture->connection, fixture->send_buffer)
                 : cnet_send(&fixture->client, fixture->connection, sent, length);
    if (fixture->measuring) {
      fixture->send_admission_ns += salts_hrtime() - started;
      ++fixture->send_admission_calls;
    }
  }
  if (status == SALTS_OK) status = io_bench_wait_cnet(fixture, &fixture->done, 1);
  if (status == SALTS_OK && fixture->send_mode != IO_BENCH_SEND_BASELINE)
    status = io_bench_wait_cnet(fixture, &fixture->send_done, 1);
  if (status == SALTS_OK && fixture->send_mode != IO_BENCH_SEND_BASELINE &&
      fixture->received != length) status = SALTS_EIO;
  if (status == SALTS_OK) status = fixture->status;
  return status;
}

static int io_bench_cnet_destroy(io_bench_cnet *fixture) {
  int status = SALTS_OK;
  if (fixture->client.impl != NULL) {
    status = cnet_close(&fixture->client, fixture->connection);
    if (status == SALTS_OK) status = io_bench_wait_cnet(fixture, &fixture->terminal, 1);
    if (status == SALTS_OK || status == SALTS_EALREADY || status == SALTS_ENOENT)
      status = cnet_client_stop(&fixture->client, IO_BENCH_TIMEOUT_MS);
    if (status == SALTS_OK) status = cnet_client_destroy(&fixture->client);
  }
  return status;
}

static int io_bench_fixture_init(io_bench_fixture *fixture, io_bench_protocol protocol,
                                 io_bench_driver driver, size_t payload_size,
                                 native_io_backend_kind backend_kind, io_bench_send_mode send_mode) {
  int status;
  memset(fixture, 0, sizeof(*fixture));
  fixture->driver = driver;
  fixture->protocol = protocol;
  fixture->server.socket_value = IO_BENCH_INVALID_SOCKET;
  fixture->native.socket_value = IO_BENCH_INVALID_SOCKET;
  status = io_bench_network_start(fixture);
  if (status == SALTS_OK) status = io_bench_server_init(&fixture->server, protocol, payload_size);
  if (status == SALTS_OK) status = io_bench_server_start(&fixture->server);
  if (status == SALTS_OK) {
    if (driver == IO_BENCH_LIBUV)
      status = io_bench_libuv_init(&fixture->libuv, protocol, &fixture->server.address);
    else if (driver == IO_BENCH_NATIVE_IO || driver == IO_BENCH_NATIVE_IO_COROUTINE)
      status =
          io_bench_native_init(&fixture->native, protocol, &fixture->server.address, backend_kind);
    else
      status = io_bench_cnet_init(&fixture->cnet, protocol, &fixture->server.address, backend_kind, send_mode);
  }
  if (status == SALTS_OK && driver == IO_BENCH_CNET)
    status = io_bench_cnet_ready(&fixture->cnet, payload_size);
  return status;
}

static int io_bench_exchange(io_bench_fixture *fixture, const unsigned char *sent,
                             unsigned char *received, size_t length) {
  if (fixture->driver == IO_BENCH_LIBUV)
    return io_bench_libuv_exchange(&fixture->libuv, sent, received, length, NULL);
  if (fixture->driver == IO_BENCH_NATIVE_IO)
    return io_bench_native_exchange(&fixture->native, sent, received, length);
  if (fixture->driver == IO_BENCH_NATIVE_IO_COROUTINE)
    return io_bench_native_coroutine_exchange(&fixture->native, sent, received, length);
  return io_bench_cnet_exchange(&fixture->cnet, sent, received, length);
}

static int io_bench_exchange_profiled(io_bench_fixture *fixture, const unsigned char *sent,
                                      unsigned char *received, size_t length,
                                      io_bench_result *result) {
  if (fixture->driver == IO_BENCH_LIBUV)
    return io_bench_libuv_exchange(&fixture->libuv, sent, received, length, result);
  if (fixture->driver == IO_BENCH_NATIVE_IO)
    return io_bench_native_exchange_profiled(&fixture->native, sent, received, length, result);
  if (fixture->driver == IO_BENCH_NATIVE_IO_COROUTINE)
    return io_bench_native_coroutine_exchange_profiled(&fixture->native, sent, received, length,
                                                       result);
  return io_bench_exchange(fixture, sent, received, length);
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
  if (status == SALTS_OK) status = server_status;
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

/* The same outer API boundaries are sampled for all four drivers. CNet's
 * payload check is nested in poll, unlike the other drivers' final memcmp. */
static io_bench_sample io_bench_snapshot(const io_bench_fixture *fixture,
                                         const io_bench_result *result) {
  if (fixture->driver == IO_BENCH_CNET)
    return (io_bench_sample){.start_ns = fixture->cnet.send_admission_ns,
                             .drive_ns = fixture->cnet.poll_ns,
                             .check_ns = fixture->cnet.benchmark_payload_check_ns,
                             .start_calls = fixture->cnet.send_admission_calls,
                             .drive_calls = fixture->cnet.poll_calls};
  return (io_bench_sample){.start_ns = result->native_start_ns,
                           .drive_ns = result->native_observe_ns,
                           .check_ns = result->payload_check_ns,
                           .start_calls = result->native_start_calls,
                           .drive_calls = result->native_observe_calls};
}

static uint64_t io_bench_cpu_ns(const uv_rusage_t *usage) {
  return ((uint64_t)usage->ru_utime.tv_sec + (uint64_t)usage->ru_stime.tv_sec) * UINT64_C(1000000000) +
         ((uint64_t)usage->ru_utime.tv_usec + (uint64_t)usage->ru_stime.tv_usec) * UINT64_C(1000);
}

static bool io_bench_trace_enabled;

static int io_bench_print_host(void) {
  uv_utsname_t host;
  uv_cpu_info_t *cpus = NULL;
  int count = 0;
  int status = uv_os_uname(&host);
  if (status != 0) return status;
  status = uv_cpu_info(&cpus, &count);
  if (status != 0) return status;
  if (count <= 0 || cpus == NULL) status = SALTS_EIO;
  else printf("Host: %s %s %s; first CPU model: %s; logical CPUs: %d; available parallelism: %u. "
              "Client/peer affinity is not pinned.\n",
              host.sysname, host.release, host.machine, cpus[0].model, count,
              uv_available_parallelism());
  uv_free_cpu_info(cpus, count);
  return status;
}

static void io_bench_free_payload(void *data, void *user) {
  (void)user;
  free(data);
}

static int io_bench_run(io_bench_protocol protocol, io_bench_driver driver, size_t payload_size,
                        bool profile_stages, native_io_backend_kind backend_kind,
                        io_bench_send_mode send_mode, io_bench_result *result) {
  io_bench_fixture fixture;
  unsigned char *sent = NULL;
  unsigned char *received = NULL;
  uint64_t *latencies = NULL;
  mem_buffer_t *send_buffer = NULL;
  uint64_t wall_started = 0u;
  size_t latency_count = 0u;
  uv_rusage_t usage_before = {0};
  uv_rusage_t usage_after = {0};
#ifdef _WIN32
  ULONG64 cycles_before = 0u, cycles_after = 0u;
#endif
  const char *phase = "init";
  int status;
  memset(result, 0, sizeof(*result));
  status = io_bench_fixture_init(&fixture, protocol, driver, payload_size, backend_kind, send_mode);
  if (status != SALTS_OK) goto cleanup;
  phase = "allocate";
  sent = (unsigned char *)malloc(payload_size);
  received = (unsigned char *)malloc(payload_size);
  latencies = (uint64_t *)malloc(sizeof(*latencies) * IO_BENCH_TOTAL_EXCHANGES);
  if (sent == NULL || received == NULL || latencies == NULL) {
    status = SALTS_ENOMEM;
    goto cleanup;
  }
  memset(sent, 0x5a, payload_size);
  if (send_mode != IO_BENCH_SEND_BASELINE) {
    /* One immutable external payload and one caller-owned reference per run,
     * identically allocated for both methods before warmup. No pool lifetime
     * can end underneath a retained send, including failed cleanup. */
    send_buffer = mem_wrap_external(sent, payload_size, io_bench_free_payload, NULL);
    if (send_buffer == NULL) { status = SALTS_ENOMEM; goto cleanup; }
    fixture.cnet.send_buffer = send_buffer;
  }
  phase = "warmup";
  for (size_t index = 0u; index < IO_BENCH_WARMUP_EXCHANGES; ++index) {
    status = io_bench_exchange(&fixture, sent, received, payload_size);
    if (status != SALTS_OK) goto cleanup;
  }
  if (driver == IO_BENCH_CNET && profile_stages) {
    status = io_bench_cnet_begin_measurement(&fixture.cnet);
    if (status != SALTS_OK) goto cleanup;
  }
  phase = "measure";
  if (io_bench_trace_enabled) {
    fprintf(stderr, "IO_BENCH_MEASURE_BEGIN rounds=%d\n", IO_BENCH_TOTAL_EXCHANGES);
    fflush(stderr);
  }
  status = uv_getrusage_thread(&usage_before);
  if (status != 0) goto cleanup;
#ifdef _WIN32
  if (!QueryThreadCycleTime(GetCurrentThread(), &cycles_before)) {
    status = uv_translate_sys_error((int)GetLastError());
    goto cleanup;
  }
#endif
  wall_started = salts_hrtime();
  for (size_t exchange = 0u; exchange < IO_BENCH_EXCHANGES_PER_REPLICATE; ++exchange) {
    const io_bench_sample before = profile_stages ? io_bench_snapshot(&fixture, result)
                                                   : (io_bench_sample){0};
    const uint64_t started = salts_hrtime();
    status = profile_stages
                 ? io_bench_exchange_profiled(&fixture, sent, received, payload_size, result)
                 : io_bench_exchange(&fixture, sent, received, payload_size);
    if (status != SALTS_OK) goto cleanup;
    const uint64_t elapsed = salts_hrtime() - started;
    io_bench_sample *sample = &result->samples[latency_count];
    latencies[latency_count++] = elapsed;
    if (profile_stages) {
      const io_bench_sample after = io_bench_snapshot(&fixture, result);
      sample->start_ns = after.start_ns - before.start_ns;
      sample->drive_ns = after.drive_ns - before.drive_ns;
      sample->check_ns = after.check_ns - before.check_ns;
      sample->start_calls = after.start_calls - before.start_calls;
      sample->drive_calls = after.drive_calls - before.drive_calls;
      if (driver == IO_BENCH_CNET) {
        if (sample->drive_ns < sample->check_ns) { status = SALTS_ERANGE; goto cleanup; }
        sample->drive_ns -= sample->check_ns;
      }
    }
    sample->wall_ns = elapsed;
  }
  result->payload_size = payload_size;
  result->round_trips = latency_count;
  result->wall_ns = salts_hrtime() - wall_started;
#ifdef _WIN32
  if (!QueryThreadCycleTime(GetCurrentThread(), &cycles_after) || cycles_after < cycles_before) {
    status = SALTS_EIO;
    goto cleanup;
  }
  result->cpu_cycles = cycles_after - cycles_before;
#endif
  status = uv_getrusage_thread(&usage_after);
  if (status != 0) goto cleanup;
  if (io_bench_trace_enabled) {
    fprintf(stderr, "IO_BENCH_MEASURE_END rounds=%d\n", IO_BENCH_TOTAL_EXCHANGES);
    fflush(stderr);
  }
  if (io_bench_cpu_ns(&usage_after) < io_bench_cpu_ns(&usage_before)) {
    status = SALTS_ERANGE;
    goto cleanup;
  }
  result->cpu_ns = io_bench_cpu_ns(&usage_after) - io_bench_cpu_ns(&usage_before);
  result->voluntary_switches = usage_after.ru_nvcsw - usage_before.ru_nvcsw;
  result->involuntary_switches = usage_after.ru_nivcsw - usage_before.ru_nivcsw;
  if (send_mode != IO_BENCH_SEND_BASELINE &&
      fixture.cnet.send_completions != IO_BENCH_ALL_EXCHANGES) {
    status = SALTS_EIO;
    goto cleanup;
  }
  qsort(latencies, latency_count, sizeof(latencies[0]), io_bench_u64_compare);
  result->p50_ns = latencies[(latency_count - 1u) * 50u / 100u];
  result->p95_ns = latencies[(latency_count - 1u) * 95u / 100u];
  if (driver == IO_BENCH_CNET && profile_stages) {
    result->cnet_receive_admission_ns = fixture.cnet.receive_admission_ns;
    result->cnet_send_admission_ns = fixture.cnet.send_admission_ns;
    result->cnet_poll_ns = fixture.cnet.poll_ns;
    result->cnet_callback_ns = fixture.cnet.callback_ns;
    result->cnet_benchmark_payload_check_ns = fixture.cnet.benchmark_payload_check_ns;
    result->cnet_receive_admission_calls = fixture.cnet.receive_admission_calls;
    result->cnet_send_admission_calls = fixture.cnet.send_admission_calls;
    result->cnet_poll_calls = fixture.cnet.poll_calls;
    result->cnet_callback_calls = fixture.cnet.callback_calls;
    status = cnet_client_profile_take(&fixture.cnet.client, &result->cnet_profile);
    fixture.cnet.measuring = false;
    if (status != SALTS_OK) goto cleanup;
  }
  status = SALTS_OK;

cleanup:
  free(latencies);
  free(received);
  {
    const int cleanup_status = io_bench_fixture_destroy(&fixture, status != SALTS_OK);
    if (status == SALTS_OK) status = cleanup_status;
  }
  if (send_buffer != NULL) {
    if (status == SALTS_OK && mem_buffer_ref_count(send_buffer) != 1u) status = SALTS_EIO;
    mem_buffer_release(send_buffer);
  } else free(sent);
  if (status != SALTS_OK)
    fprintf(stderr, "benchmark driver=%s protocol=%s payload=%zu send_mode=%d phase=%s status=%d\n",
            io_bench_driver_name(driver), protocol == IO_BENCH_TCP ? "TCP" : "UDP", payload_size,
            (int)send_mode, phase, status);
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

static int io_bench_series_finalize_comparison(io_bench_series *series) {
  int status;
  series->payload_size = series->runs[0].payload_size;
  status = io_bench_series_summarize(series, IO_BENCH_METRIC_P50, &series->p50_ns);
  if (status == SALTS_OK)
    status = io_bench_series_summarize(series, IO_BENCH_METRIC_P95, &series->p95_ns);
  if (status == SALTS_OK)
    status = io_bench_series_summarize(series, IO_BENCH_METRIC_RATE, &series->rate_per_second);
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
  printf("\n%s %s paired latency delta versus libuv\n", protocol, percentile);
  printf("| payload | NativeIO direct median +/- MAD | NativeIO coroutine median +/- MAD | "
         "CNet median +/- MAD |\n");
  printf("| ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    cnet_benchmark_summary coroutine_delta = {0};
    cnet_benchmark_summary cnet_delta = {0};
    cnet_benchmark_summary native_delta = {0};
    int status = io_bench_paired_delta(&libuv[index], &coroutine[index], metric, &coroutine_delta);
    if (status == SALTS_OK)
      status = io_bench_paired_delta(&libuv[index], &cnet[index], metric, &cnet_delta);
    if (status == SALTS_OK)
      status = io_bench_paired_delta(&libuv[index], &native[index], metric, &native_delta);
    if (status != SALTS_OK) return status;
    printf("| %zu KiB | %+.2f%% +/- %.2fpp | %+.2f%% +/- %.2fpp | %+.2f%% +/- %.2fpp |\n",
           libuv[index].payload_size / 1024u, native_delta.median, native_delta.mad,
           coroutine_delta.median, coroutine_delta.mad, cnet_delta.median, cnet_delta.mad);
  }
  return SALTS_OK;
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
  printf("\n%s paired rate delta versus libuv\n", protocol);
  printf("| payload | NativeIO direct median +/- MAD | NativeIO coroutine median +/- MAD | "
         "CNet median +/- MAD |\n");
  printf("| ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    cnet_benchmark_summary coroutine_delta = {0};
    cnet_benchmark_summary cnet_delta = {0};
    cnet_benchmark_summary native_delta = {0};
    int status = io_bench_paired_delta(&libuv[index], &coroutine[index], IO_BENCH_METRIC_RATE,
                                       &coroutine_delta);
    if (status == SALTS_OK)
      status =
          io_bench_paired_delta(&libuv[index], &cnet[index], IO_BENCH_METRIC_RATE, &cnet_delta);
    if (status == SALTS_OK)
      status =
          io_bench_paired_delta(&libuv[index], &native[index], IO_BENCH_METRIC_RATE, &native_delta);
    if (status != SALTS_OK) return status;
    printf("| %zu KiB | %+.2f%% +/- %.2fpp | %+.2f%% +/- %.2fpp | %+.2f%% +/- %.2fpp |\n",
           libuv[index].payload_size / 1024u, native_delta.median, native_delta.mad,
           coroutine_delta.median, coroutine_delta.mad, cnet_delta.median, cnet_delta.mad);
  }
  return SALTS_OK;
}

static int io_bench_fixed_control_attribution(
    const io_bench_result *result, cnet_benchmark_fixed_control_attribution *out_attribution) {
  const cnet_owner_profile *owner;
  cnet_benchmark_fixed_control_sample sample;

  if (result == NULL || out_attribution == NULL || result->round_trips == 0u) return SALTS_EINVAL;
  owner = &result->cnet_profile.owner;
  if (result->cnet_poll_ns < result->cnet_profile.client_poll_ns) return SALTS_ERANGE;
  sample = (cnet_benchmark_fixed_control_sample){
      .round_trips = result->round_trips,
      .send_admit_ns = result->cnet_send_admission_ns,
      .queue_publish_ns = owner->command_queue_payload_publish_ns,
      .payload_copy_ns = owner->command_queue_payload_copy_ns,
      .client_poll_ns = result->cnet_poll_ns,
      .owner_drive_ns = owner->owner_drive_ns,
      .request_lifecycle_ns = owner->request_lifecycle_ns,
      .request_start_ns = owner->request_start_ns,
      .request_resubmit_ns = owner->request_resubmit_ns,
      .observe_ns = owner->observe_ns,
      .request_completion_ns = owner->request_completion_ns,
      .event_publish_ns = owner->event_publish_ns,
      .dispatcher_prepare_ns = result->cnet_profile.dispatcher_prepare_ns,
      .dispatcher_invoke_ns = result->cnet_profile.dispatcher_invoke_ns,
      .dispatcher_observer_ns = result->cnet_profile.dispatcher_observer_ns,
      .dispatcher_release_ns = result->cnet_profile.dispatcher_release_ns,
      .benchmark_callback_ns = result->cnet_callback_ns,
      .benchmark_payload_check_ns = result->cnet_benchmark_payload_check_ns};
  return cnet_benchmark_attribute_fixed_control(&sample, out_attribution);
}

static int io_bench_phase_budget(const io_bench_result *result,
                                  cnet_benchmark_phase_budget *budget) {
  cnet_benchmark_phase_sample sample = {.wall_ns = result->wall_ns};
  for (size_t i = 0u; i < result->round_trips; ++i) {
    sample.start_ns += result->samples[i].start_ns;
    sample.drive_ns += result->samples[i].drive_ns;
    sample.check_ns += result->samples[i].check_ns;
  }
  return cnet_benchmark_phase_decompose(&sample, result->round_trips, budget);
}

static int io_bench_print_diagnostics(const char *protocol, const io_bench_series *libuv,
                                      const io_bench_series *native,
                                      const io_bench_series *coroutine,
                                      const io_bench_series *cnet, size_t count) {
  const io_bench_series *drivers[] = {libuv, native, coroutine, cnet};
  printf("\n%s measurement quality and client-thread cost\n", protocol);
  printf("A/A is paired B/A p50 median +/- MAD, not a confidence interval. Probe drift is "
         "diagnostic mean RTT versus the mean of its surrounding A/B runs. CPU spans the whole "
         "batch; wall-CPU includes blocked AND runnable time, not just kernel wait. CPU accounting "
         "can be coarse, especially on Windows; a negative estimate is retained, never clamped.\n");
#ifdef _WIN32
  printf("Windows CPU cost is QueryThreadCycleTime cycles/RT, never converted to time. "
         "Coarse CPU ns remain in CSV; wall-CPU is unresolved, not reported as precise wait.\n");
#else
  printf("Client CPU cost is microseconds/RT.\n");
#endif
  printf("| payload | driver | A/A p50 %% +/- MAD pp | probe mean %% +/- MAD pp | "
         "diagnostic mean us | client CPU cost/RT | wall-CPU us/RT |\n");
  printf("| ---: | --- | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    for (unsigned driver = 0u; driver < IO_BENCH_DRIVER_COUNT; ++driver) {
      const io_bench_series *series = &drivers[driver][index];
      double a[IO_BENCH_REPLICATES], b[IO_BENCH_REPLICATES];
      double unprofiled[IO_BENCH_REPLICATES], profiled[IO_BENCH_REPLICATES];
      double cpu = 0.0, wall = 0.0;
      cnet_benchmark_summary noise = {0}, probe = {0};
      for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
        const io_bench_result *raw = &series->runs[repeat];
        const io_bench_result *control = &series->control_runs[repeat];
        const io_bench_result *diag = &series->stage_profile_runs[repeat];
        a[repeat] = (double)raw->p50_ns;
        b[repeat] = (double)control->p50_ns;
        unprofiled[repeat] = (io_bench_mean(raw->wall_ns, raw->round_trips) +
                              io_bench_mean(control->wall_ns, control->round_trips)) / 2.0;
        profiled[repeat] = io_bench_mean(diag->wall_ns, diag->round_trips);
#ifdef _WIN32
        cpu += io_bench_mean(diag->cpu_cycles, diag->round_trips) / IO_BENCH_REPLICATES;
#else
        cpu += io_bench_mean(diag->cpu_ns, diag->round_trips) / IO_BENCH_REPLICATES;
#endif
        wall += profiled[repeat] / IO_BENCH_REPLICATES;
      }
      int status = cnet_benchmark_summarize_paired_delta(a, b, IO_BENCH_REPLICATES, &noise);
      if (status == SALTS_OK)
        status = cnet_benchmark_summarize_paired_delta(unprofiled, profiled, IO_BENCH_REPLICATES,
                                                       &probe);
      if (status != SALTS_OK) return status;
      printf("| %zu KiB | %s | %+.2f +/- %.2f | %+.2f +/- %.2f | %.3f | ",
             series->payload_size / 1024u, io_bench_driver_name((io_bench_driver)driver),
             noise.median, noise.mad, probe.median, probe.mad, wall / 1000.0);
#ifdef _WIN32
      printf("%.0f cycles | unresolved |\n", cpu);
#else
      printf("%.3f us | %+.3f |\n", cpu / 1000.0, (wall - cpu) / 1000.0);
#endif
    }
  }

  printf("\n%s diagnostic boundary deltas versus libuv (us/RT)\n", protocol);
  printf("Arithmetic mean of paired per-repeat differences, NOT differences of p50/p95. "
         "Start + drive + check + remainder = diagnostic mean gap only. "
         "Start: uv read-arm/write, NativeIO prepare, coroutine spawn, or CNet send admission. "
         "Drive: uv_run, backend observe, or CNet poll, excluding payload verification. "
         "NativeIO observe includes flush; CNet executes queued preparation inside drive. "
         "These different API boundaries locate work "
         "but do NOT prove a CPU or syscall cause. Drive still mixes wait and dispatch. "
         "Remainder includes harness and probe bookkeeping; unresolved causes require a trace.\n");
  printf("| payload | driver | mean gap | start delta | drive delta | check delta | remainder delta |\n");
  printf("| ---: | --- | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    for (unsigned driver = 1u; driver < IO_BENCH_DRIVER_COUNT; ++driver) {
      cnet_benchmark_phase_budget delta = {0};
      for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
        cnet_benchmark_phase_budget reference = {0}, candidate = {0};
        int status = io_bench_phase_budget(&libuv[index].stage_profile_runs[repeat], &reference);
        if (status == SALTS_OK)
          status = io_bench_phase_budget(&drivers[driver][index].stage_profile_runs[repeat],
                                         &candidate);
        if (status != SALTS_OK) return status;
        delta.wall_ns += (candidate.wall_ns - reference.wall_ns) / IO_BENCH_REPLICATES;
        delta.start_ns += (candidate.start_ns - reference.start_ns) / IO_BENCH_REPLICATES;
        delta.drive_ns += (candidate.drive_ns - reference.drive_ns) / IO_BENCH_REPLICATES;
        delta.check_ns += (candidate.check_ns - reference.check_ns) / IO_BENCH_REPLICATES;
        delta.remainder_ns += (candidate.remainder_ns - reference.remainder_ns) / IO_BENCH_REPLICATES;
      }
      printf("| %zu KiB | %s | %+.3f | %+.3f | %+.3f | %+.3f | %+.3f |\n",
             libuv[index].payload_size / 1024u, io_bench_driver_name((io_bench_driver)driver),
             delta.wall_ns / 1000.0, delta.start_ns / 1000.0, delta.drive_ns / 1000.0,
             delta.check_ns / 1000.0, delta.remainder_ns / 1000.0);
    }
  }

  printf("\n%s CNet diagnostic internal evidence (mean us/RT)\n", protocol);
  printf("Within CNet only, not an explanation of the libuv gap. "
         "Observe still includes waiting. Payload copy is the admitted send copy only.\n");
  printf("| payload | fixed control | payload copy | NativeIO prepare/reprepare | observe + flush | starts/RT | completions/RT |\n");
  printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    double fixed = 0.0, copy = 0.0, submit = 0.0, observe = 0.0, starts = 0.0, completions = 0.0;
    for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
      const io_bench_result *result = &cnet[index].stage_profile_runs[repeat];
      cnet_benchmark_fixed_control_attribution attribution;
      const int status = io_bench_fixed_control_attribution(result, &attribution);
      if (status != SALTS_OK) return status;
      fixed += attribution.fixed_control_total_ns / IO_BENCH_REPLICATES;
      copy += attribution.payload_copy_ns / IO_BENCH_REPLICATES;
      submit += (attribution.native_request_start_ns + attribution.native_request_resubmit_ns) /
                IO_BENCH_REPLICATES;
      observe += attribution.native_observe_ns / IO_BENCH_REPLICATES;
      starts += io_bench_mean(result->cnet_profile.owner.request_start_calls, result->round_trips) /
                IO_BENCH_REPLICATES;
      completions += io_bench_mean(result->cnet_profile.owner.request_completion_calls,
                                   result->round_trips) / IO_BENCH_REPLICATES;
    }
    printf("| %zu KiB | %.3f | %.3f | %.3f | %.3f | %.2f | %.2f |\n",
           cnet[index].payload_size / 1024u, fixed / 1000.0, copy / 1000.0, submit / 1000.0,
           observe / 1000.0, starts, completions);
  }
  return SALTS_OK;
}

/* Artifacts are written only AFTER all timed runs. The fixed sample arrays bound
 * memory by payload rows * drivers * passes * repeats * exchanges, never traffic. */
static int io_bench_csv_line(salts_file_t file, const char *format, ...) {
  char line[IO_BENCH_CSV_LINE_CAPACITY];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length < 0 || (size_t)length >= sizeof(line)) return SALTS_ERANGE;
  size_t offset = 0u;
  while (offset < (size_t)length) {
    const int written = salts_fs_write(file, line + offset, (size_t)length - offset);
    if (written < 0) return written;
    if (written == 0) return SALTS_EIO;
    offset += (size_t)written;
  }
  return SALTS_OK;
}

static int io_bench_write_series(salts_file_t runs, salts_file_t samples,
                                  const char *backend, const char *protocol,
                                  const char *driver, const io_bench_series *series,
                                  bool include_diagnostic) {
  const io_bench_result *passes[] = {series->runs, series->stage_profile_runs, series->control_runs};
  static const char *const names[] = {"A", "diagnostic", "B"};
  for (unsigned pass = 0u; pass < IO_BENCH_PASS_COUNT; ++pass) {
    if (pass == IO_BENCH_PASS_DIAGNOSTIC && !include_diagnostic) continue;
    for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
      const io_bench_result *result = &passes[pass][repeat];
      size_t start_calls = 0u, drive_calls = 0u;
      for (size_t i = 0u; i < result->round_trips; ++i) {
        const io_bench_sample *sample = &result->samples[i];
        cnet_benchmark_phase_sample phases = {sample->wall_ns, sample->start_ns,
                                               sample->drive_ns, sample->check_ns};
        cnet_benchmark_phase_budget budget;
        int status = cnet_benchmark_phase_decompose(&phases, 1u, &budget);
        if (status != SALTS_OK) return status;
        start_calls += sample->start_calls;
        drive_calls += sample->drive_calls;
        if (pass == IO_BENCH_PASS_DIAGNOSTIC)
          status = io_bench_csv_line(samples, "%s,%s,%zu,%s,%s,%zu,%zu,%llu,%llu,%llu,%llu,%.0f,%zu,%zu\n",
              backend, protocol, result->payload_size, driver, names[pass],
              repeat + 1u, i + 1u, (unsigned long long)sample->wall_ns,
              (unsigned long long)sample->start_ns, (unsigned long long)sample->drive_ns,
              (unsigned long long)sample->check_ns, budget.remainder_ns,
              sample->start_calls, sample->drive_calls);
        else
          status = io_bench_csv_line(samples, "%s,%s,%zu,%s,%s,%zu,%zu,%llu,,,,,,\n",
              backend, protocol, result->payload_size, driver, names[pass],
              repeat + 1u, i + 1u, (unsigned long long)sample->wall_ns);
        if (status != SALTS_OK) return status;
      }
      char switches[IO_BENCH_CSV_LINE_CAPACITY];
#ifdef __linux__
      (void)snprintf(switches, sizeof(switches), "%llu,%llu",
                     (unsigned long long)result->voluntary_switches,
                     (unsigned long long)result->involuntary_switches);
#else
      /* libuv does not provide these thread counters on Windows/macOS. */
      (void)snprintf(switches, sizeof(switches), ",");
#endif
      int status = io_bench_csv_line(runs, "%s,%s,%zu,%s,%s,%zu,%zu,%llu,%llu,%llu,%llu,%s,",
          backend, protocol, result->payload_size, driver, names[pass],
          repeat + 1u, result->round_trips, (unsigned long long)result->wall_ns,
          (unsigned long long)result->cpu_ns, (unsigned long long)result->p50_ns,
          (unsigned long long)result->p95_ns, switches);
      if (status == SALTS_OK)
        status = pass == IO_BENCH_PASS_DIAGNOSTIC ? io_bench_csv_line(runs, "%zu,%zu,", start_calls, drive_calls)
                            : io_bench_csv_line(runs, ",,");
#ifdef _WIN32
      if (status == SALTS_OK)
        status = io_bench_csv_line(runs, "%llu\n", (unsigned long long)result->cpu_cycles);
#else
      if (status == SALTS_OK) status = io_bench_csv_line(runs, "\n");
#endif
      if (status != SALTS_OK) return status;
    }
  }
  return SALTS_OK;
}

typedef struct io_bench_dataset {
  const char *protocol;
  const char *driver;
  const io_bench_series *rows;
  size_t count;
  bool include_diagnostic;
} io_bench_dataset;

static int io_bench_write_artifacts(const char *prefix, const char *backend,
                                     const io_bench_dataset *datasets, size_t count) {
  char path[IO_BENCH_CSV_LINE_CAPACITY];
  salts_file_t runs = SALTS_INVALID_FILE, samples = SALTS_INVALID_FILE;
  int status = SALTS_OK;
  if (prefix == NULL) return SALTS_OK;
  if (*prefix == '\0') return SALTS_EINVAL;
  int length = snprintf(path, sizeof(path), "%s.runs.csv", prefix);
  if (length < 0 || (size_t)length >= sizeof(path)) return SALTS_ERANGE;
  runs = salts_fs_open(path, SALTS_FS_O_WRONLY | SALTS_FS_O_CREAT | SALTS_FS_O_TRUNC,
                      SALTS_FS_DEFAULT_MODE);
  if (runs == SALTS_INVALID_FILE) return SALTS_EIO;
  length = snprintf(path, sizeof(path), "%s.samples.csv", prefix);
  if (length < 0 || (size_t)length >= sizeof(path)) { status = SALTS_ERANGE; goto cleanup; }
  samples = salts_fs_open(path, SALTS_FS_O_WRONLY | SALTS_FS_O_CREAT | SALTS_FS_O_TRUNC,
                         SALTS_FS_DEFAULT_MODE);
  if (samples == SALTS_INVALID_FILE) { status = SALTS_EIO; goto cleanup; }
  status = io_bench_csv_line(runs, "backend,protocol,payload_bytes,driver,pass,repeat,round_trips,"
      "wall_ns,client_cpu_ns,p50_ns,p95_ns,voluntary_switches,involuntary_switches,start_calls,drive_calls,client_cpu_cycles\n");
  if (status == SALTS_OK)
    status = io_bench_csv_line(samples, "backend,protocol,payload_bytes,driver,pass,repeat,sample,"
        "wall_ns,start_ns,drive_ns,check_ns,remainder_ns,start_calls,drive_calls\n");
  for (size_t dataset = 0u; status == SALTS_OK && dataset < count; ++dataset) {
    const io_bench_dataset *data = &datasets[dataset];
    for (size_t row = 0u; status == SALTS_OK && row < data->count; ++row)
      status = io_bench_write_series(runs, samples, backend, data->protocol, data->driver,
                                     &data->rows[row], data->include_diagnostic);
  }
cleanup:
  if (samples != SALTS_INVALID_FILE) {
    const int close_status = salts_fs_close(samples);
    if (status == SALTS_OK) status = close_status;
  }
  {
    const int close_status = salts_fs_close(runs);
    if (status == SALTS_OK) status = close_status;
  }
  if (status != SALTS_OK)
    fprintf(stderr, "benchmark artifact write failed: prefix=%s status=%d\n", prefix, status);
  return status;
}

static int io_bench_run_row(io_bench_protocol protocol, size_t payload, size_t row,
                            io_bench_series *libuv, io_bench_series *native,
                            io_bench_series *coroutine, io_bench_series *cnet,
                            native_io_backend_kind backend_kind) {
  io_bench_series *series[] = {libuv, native, coroutine, cnet};
  /* Sandwich the diagnostic quartet between identical uninstrumented quartets.
   * Reversing the outer order every repeat prevents A/B from naming time order. */
  for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
    for (unsigned pass = 0u; pass < IO_BENCH_PASS_COUNT; ++pass) {
      for (unsigned index = 0u; index < IO_BENCH_DRIVER_COUNT; ++index) {
        const io_bench_driver driver = (io_bench_driver)((row + repeat + index) % IO_BENCH_DRIVER_COUNT);
        io_bench_result *result = pass == IO_BENCH_PASS_DIAGNOSTIC ? &series[driver]->stage_profile_runs[repeat]
            : ((pass == IO_BENCH_PASS_A) == ((repeat & 1u) == 0u)) ? &series[driver]->runs[repeat]
                                                     : &series[driver]->control_runs[repeat];
        const io_bench_send_mode send_mode =
            driver == IO_BENCH_CNET ? IO_BENCH_SEND_RETAINED : IO_BENCH_SEND_BASELINE;
        const int status = io_bench_run(protocol, driver, payload, pass == IO_BENCH_PASS_DIAGNOSTIC,
                                        backend_kind, send_mode, result);
        if (status != SALTS_OK) return status;
      }
    }
  }
  for (size_t driver = 0u; driver < IO_BENCH_DRIVER_COUNT; ++driver) {
    const int status = io_bench_series_finalize_comparison(series[driver]);
    if (status != SALTS_OK) return status;
  }
  return SALTS_OK;
}

static int io_bench_compare_sends(const cnet_io_benchmark_backend *backend, const char *prefix) {
  enum { SEND_METHODS = 2, CONTROL_PASSES = 2,
         TCP_ROWS = sizeof(IO_BENCH_TCP_PAYLOADS) / sizeof(IO_BENCH_TCP_PAYLOADS[0]) };
  static io_bench_series series[SEND_METHODS][TCP_ROWS];
  const io_bench_send_mode modes[SEND_METHODS] = {IO_BENCH_SEND_COPY, IO_BENCH_SEND_RETAINED};
  const io_bench_dataset datasets[SEND_METHODS] = {
      {"TCP", "CNet copy control", series[0], TCP_ROWS, false},
      {"TCP", "CNet retained", series[1], TCP_ROWS, false}};
  int status = io_bench_print_host();
  if (status != SALTS_OK) return status;
  printf("\nSeparate CNet send comparison: backend=%s, %d repeats, %d warmups, %d RTTs/run.\n",
         backend->name, IO_BENCH_REPLICATES, IO_BENCH_WARMUP_EXCHANGES, IO_BENCH_TOTAL_EXCHANGES);
  printf("Both methods reuse an immutable externally wrapped payload and wait for send completion "
         "plus checked echo. Only cnet_send versus cnet_send_buffer changes. "
         "A/B have no stage probes; pair order reverses between A/B and rotates by row/repeat. "
         "This measures the full admission/ownership path difference, NOT isolated memcpy cost.\n");
  for (size_t row = 0u; row < TCP_ROWS; ++row) {
    for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
      for (unsigned pass = 0u; pass < CONTROL_PASSES; ++pass) {
        const bool pass_a = (pass == 0u) == ((repeat & 1u) == 0u);
        for (unsigned index = 0u; index < SEND_METHODS; ++index) {
          const size_t method = (row + repeat + (pass_a ? 0u : 1u) + index) % SEND_METHODS;
          io_bench_result *result = pass_a
              ? &series[method][row].runs[repeat] : &series[method][row].control_runs[repeat];
          status = io_bench_run(IO_BENCH_TCP, IO_BENCH_CNET, IO_BENCH_TCP_PAYLOADS[row],
                                 false, backend->kind, modes[method], result);
          if (status != SALTS_OK) return status;
        }
      }
    }
  }
  printf("\nPaired retained minus copy p50 delta; negative means retained is faster.\n");
  printf("| payload | pass | median delta | MAD |\n| ---: | --- | ---: | ---: |\n");
  for (size_t row = 0u; row < TCP_ROWS; ++row) {
    for (unsigned pass = 0u; pass < CONTROL_PASSES; ++pass) {
      double copy[IO_BENCH_REPLICATES], retained[IO_BENCH_REPLICATES];
      cnet_benchmark_summary delta;
      for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
        copy[repeat] = (double)(pass == 0u ? series[0][row].runs[repeat].p50_ns
                                          : series[0][row].control_runs[repeat].p50_ns);
        retained[repeat] = (double)(pass == 0u ? series[1][row].runs[repeat].p50_ns
                                              : series[1][row].control_runs[repeat].p50_ns);
      }
      status = cnet_benchmark_summarize_paired_delta(copy, retained, IO_BENCH_REPLICATES, &delta);
      if (status != SALTS_OK) return status;
      printf("| %zu KiB | %s | %+.2f%% | %.2fpp |\n", IO_BENCH_TCP_PAYLOADS[row] / 1024u,
             pass == 0u ? "A" : "B", delta.median, delta.mad);
    }
  }
  return io_bench_write_artifacts(prefix, backend->name, datasets, SEND_METHODS);
}

spec("libuv versus NativeIO direct versus NativeIO coroutine versus CNet benchmark") {
  it("compares persistent TCP and UDP clients against one common echo peer") {
    cnet_io_benchmark_backend backend = {0};
    cnet_io_benchmark_trace trace = {0};
    const char *requested_backend = getenv("CNET_IO_BENCHMARK_BACKEND");
    const char *requested_trace = getenv("CNET_IO_BENCHMARK_TRACE");
    const char *output_prefix = getenv("CNET_IO_BENCHMARK_OUTPUT");
    const char *requested_send_comparison = getenv("CNET_IO_BENCHMARK_SEND_COMPARE");
    bool send_comparison = false;
    int status = cnet_io_benchmark_select_backend(requested_backend, &backend);
    if (status != SALTS_OK)
      fprintf(stderr, "CNET_IO_BENCHMARK_BACKEND selection failed: value='%s', status=%d\n",
              requested_backend == NULL ? "<unset>" : requested_backend, status);
    check_equal(status, SALTS_OK);
    if (status != SALTS_OK) return;
    status = cnet_io_benchmark_select_trace(requested_trace, &trace);
    if (status != SALTS_OK)
      fprintf(stderr, "CNET_IO_BENCHMARK_TRACE selection failed: value='%s', status=%d\n",
              requested_trace == NULL ? "<unset>" : requested_trace, status);
    check_equal(status, SALTS_OK);
    if (status != SALTS_OK) return;
    status = cnet_io_benchmark_select_send_comparison(requested_send_comparison, trace.enabled,
                                                       &send_comparison);
    if (status != SALTS_OK)
      fprintf(stderr, "CNET_IO_BENCHMARK_SEND_COMPARE must be unset or 1, and cannot combine with TRACE\n");
    check_equal(status, SALTS_OK);
    if (status != SALTS_OK) return;
    if (send_comparison) {
      check_equal(io_bench_compare_sends(&backend, output_prefix), SALTS_OK);
      return;
    }
    if (trace.enabled) {
      static io_bench_result traced;
      printf("TRACE ONLY: %s backend=%s libuv=%s; not a performance score.\n",
             requested_trace, backend.name, uv_version_string());
      io_bench_trace_enabled = true;
      {
        const io_bench_driver trace_driver = (io_bench_driver)trace.driver;
        const io_bench_send_mode send_mode =
            trace_driver == IO_BENCH_CNET ? IO_BENCH_SEND_RETAINED : IO_BENCH_SEND_BASELINE;
        status = io_bench_run(trace.udp ? IO_BENCH_UDP : IO_BENCH_TCP,
                              trace_driver, trace.payload_size, false, backend.kind,
                              send_mode, &traced);
      }
      io_bench_trace_enabled = false;
      check_equal(status, SALTS_OK);
      return;
    }

    enum { TCP_ROWS = sizeof(IO_BENCH_TCP_PAYLOADS) / sizeof(IO_BENCH_TCP_PAYLOADS[0]),
           UDP_ROWS = sizeof(IO_BENCH_UDP_PAYLOADS) / sizeof(IO_BENCH_UDP_PAYLOADS[0]) };
    /* Retain bounded raw samples until every timed run finishes; no file writes
     * or per-exchange logging may perturb the next comparison run. */
    static io_bench_series tcp[IO_BENCH_DRIVER_COUNT][TCP_ROWS];
    static io_bench_series udp[IO_BENCH_DRIVER_COUNT][UDP_ROWS];
    io_bench_dataset datasets[IO_BENCH_DRIVER_COUNT * 2u];
    for (unsigned driver = 0u; driver < IO_BENCH_DRIVER_COUNT; ++driver) {
      datasets[driver * 2u] = (io_bench_dataset){"TCP", io_bench_driver_name((io_bench_driver)driver),
                                                tcp[driver], TCP_ROWS, true};
      datasets[driver * 2u + 1u] = (io_bench_dataset){"UDP", io_bench_driver_name((io_bench_driver)driver),
                                                     udp[driver], UDP_ROWS, true};
    }

    printf("\nReference: libuv %s; NativeIO backend: %s; CNet: retained/owned payload API.\n",
           uv_version_string(), backend.name);
    status = io_bench_print_host();
    check_equal(status, SALTS_OK);
    if (status != SALTS_OK) return;
    printf("Fresh client and blocking echo peer per run. All four drivers rotate within each "
           "quartet. Every repeat sandwiches diagnostic quartets between uninstrumented A/B "
           "quartets; A/B time order alternates. A/A is collected on every platform and driver.\n");
    printf("CNet uses one bounded receive demand and borrowed callback views. Deadlines are "
           "disabled; timeout behavior belongs to contract tests.\n");
    printf("NativeIO direct/coroutine and CNet opt into prepare/observe batching; "
           "io_uring flushes eligible lane heads together, readiness/IOCP start immediately.\n");
    printf("Workload: %d repeats; %d warmups and %d sequential persistent round trips per run. "
           "RT/s is NOT concurrent saturation throughput.\n",
           IO_BENCH_REPLICATES, IO_BENCH_WARMUP_EXCHANGES, IO_BENCH_EXCHANGES_PER_REPLICATE);
    printf("Main p50/p95/rate use A runs and paired median/MAD. Diagnostic means are separate; "
           "do not subtract them from main percentiles or claim probe drift is pure probe cost.\n");
    printf("Raw sample storage is bounded to %zu bytes and written after all measurements.\n",
           sizeof(tcp) + sizeof(udp));
    printf("CPU uses uv_getrusage_thread at batch boundaries (never process-wide CPU). "
           "Unsupported context-switch fields are blank in CSV, not zero.\n");

    for (size_t row = 0u; row < TCP_ROWS; ++row) {
      status = io_bench_run_row(IO_BENCH_TCP, IO_BENCH_TCP_PAYLOADS[row], row, &tcp[0][row],
                                &tcp[1][row], &tcp[2][row], &tcp[3][row], backend.kind);
      check_equal(status, SALTS_OK);
      if (status != SALTS_OK) return;
    }
    for (size_t row = 0u; row < UDP_ROWS; ++row) {
      status = io_bench_run_row(IO_BENCH_UDP, IO_BENCH_UDP_PAYLOADS[row], row, &udp[0][row],
                                &udp[1][row], &udp[2][row], &udp[3][row], backend.kind);
      check_equal(status, SALTS_OK);
      if (status != SALTS_OK) return;
    }

    check_equal(io_bench_print_latency("TCP", "p50", tcp[0], tcp[1], tcp[2], tcp[3], TCP_ROWS, false),
                SALTS_OK);
    check_equal(io_bench_print_latency("TCP", "p95", tcp[0], tcp[1], tcp[2], tcp[3], TCP_ROWS, true),
                SALTS_OK);
    check_equal(io_bench_print_rate("TCP", tcp[0], tcp[1], tcp[2], tcp[3], TCP_ROWS), SALTS_OK);
    check_equal(io_bench_print_diagnostics("TCP", tcp[0], tcp[1], tcp[2], tcp[3], TCP_ROWS), SALTS_OK);
    check_equal(io_bench_print_latency("UDP", "p50", udp[0], udp[1], udp[2], udp[3], UDP_ROWS, false),
                SALTS_OK);
    check_equal(io_bench_print_latency("UDP", "p95", udp[0], udp[1], udp[2], udp[3], UDP_ROWS, true),
                SALTS_OK);
    check_equal(io_bench_print_rate("UDP", udp[0], udp[1], udp[2], udp[3], UDP_ROWS), SALTS_OK);
    check_equal(io_bench_print_diagnostics("UDP", udp[0], udp[1], udp[2], udp[3], UDP_ROWS), SALTS_OK);
    check_equal(io_bench_write_artifacts(output_prefix, backend.name, datasets,
                                          sizeof(datasets) / sizeof(datasets[0])), SALTS_OK);
    printf("Raw artifacts: %s\n", output_prefix == NULL
        ? "not requested; set CNET_IO_BENCHMARK_OUTPUT to a path prefix" : output_prefix);
  }
}
