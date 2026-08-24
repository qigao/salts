#include "tinytest.h"

#include <cflow/io_native.h>

#include <turbo/clock.h>
#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#if defined(interface)
#undef interface
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <psapi.h>
typedef SOCKET network_socket;
#define NETWORK_INVALID_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int network_socket;
#define NETWORK_INVALID_SOCKET (-1)
#endif

enum {
  NETWORK_REQUEST_CAPACITY = 2u,
  NETWORK_LATENCY_SAMPLES = 200u,
  NETWORK_LATENCY_EXCHANGES = 128u,
  NETWORK_LATENCY_PAYLOAD = 64u,
  NETWORK_THROUGHPUT_SAMPLES = 50u,
  NETWORK_THROUGHPUT_EXCHANGES = 128u,
  NETWORK_TCP_THROUGHPUT_PAYLOAD = 16384u,
  NETWORK_UDP_THROUGHPUT_PAYLOAD = 8192u,
  NETWORK_MAX_SAMPLES = 10000u,
  NETWORK_MAX_EXCHANGES = 10000u,
  NETWORK_MAX_PAYLOAD = 65507u
};

static const uint64_t NETWORK_WAIT_TIMEOUT_NS = UINT64_C(5000000000);

typedef enum network_protocol {
  NETWORK_PROTOCOL_TCP = 0,
  NETWORK_PROTOCOL_UDP
} network_protocol;

typedef struct network_operation {
  cflow_io_native_operation native;
} network_operation;

typedef struct network_completion_probe {
  cflow_io_request_id ids[NETWORK_REQUEST_CAPACITY];
  cflow_io_completion values[NETWORK_REQUEST_CAPACITY];
  size_t count;
} network_completion_probe;

typedef struct network_server {
  network_protocol protocol;
  network_socket socket_value;
  size_t exchanges;
  size_t payload_size;
  unsigned char *buffer;
  int status;
} network_server;

typedef struct network_fixture {
  cflow_io_native_backend backend;
  cflow_executor executor;
  cflow_io_actor actor;
  network_completion_probe completions;
  network_socket client_socket;
  network_socket server_socket;
  struct sockaddr_in server_address;
  turbo_thread_t server_thread;
  network_server server;
  bool actor_initialized;
  bool executor_initialized;
  bool backend_initialized;
  bool server_started;
} network_fixture;

typedef struct network_measurement {
  uint64_t *latencies;
  size_t latency_capacity;
  size_t latency_count;
  uint64_t wall_ns;
  uint64_t cpu_ns;
  uint64_t peak_rss_bytes;
} network_measurement;

static int network_last_error(void) {
#if defined(_WIN32)
  return -WSAGetLastError();
#else
  return -errno;
#endif
}

static void network_close(network_socket socket_value) {
  if (socket_value == NETWORK_INVALID_SOCKET) return;
#if defined(_WIN32)
  (void)closesocket(socket_value);
#else
  (void)close(socket_value);
#endif
}

static int network_set_nonblocking(network_socket socket_value) {
#if defined(_WIN32)
  u_long enabled = 1u;
  return ioctlsocket(socket_value, FIONBIO, &enabled) == 0
             ? TURBO_OK : network_last_error();
#else
  int flags;
  do {
    flags = fcntl(socket_value, F_GETFL);
  } while (flags < 0 && errno == EINTR);
  if (flags < 0) return -errno;
  do {
    flags = fcntl(socket_value, F_SETFL, flags | O_NONBLOCK);
  } while (flags < 0 && errno == EINTR);
  return flags == 0 ? TURBO_OK : -errno;
#endif
}

static int network_set_server_timeout(network_socket socket_value) {
#if defined(_WIN32)
  const DWORD timeout_ms = 5000u;
  if (setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO,
                 (const char *)&timeout_ms, sizeof(timeout_ms)) != 0 ||
      setsockopt(socket_value, SOL_SOCKET, SO_SNDTIMEO,
                 (const char *)&timeout_ms, sizeof(timeout_ms)) != 0)
    return network_last_error();
#else
  const struct timeval timeout = {5, 0};
  if (setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO,
                 &timeout, sizeof(timeout)) != 0 ||
      setsockopt(socket_value, SOL_SOCKET, SO_SNDTIMEO,
                 &timeout, sizeof(timeout)) != 0)
    return network_last_error();
#endif
  return TURBO_OK;
}

static int network_bind_loopback(network_socket socket_value,
                                 struct sockaddr_in *address) {
#if defined(_WIN32)
  int address_length = (int)sizeof(*address);
#else
  socklen_t address_length = (socklen_t)sizeof(*address);
#endif
  memset(address, 0, sizeof(*address));
  address->sin_family = AF_INET;
  address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address->sin_port = 0;
  if (bind(socket_value, (const struct sockaddr *)address,
           (int)sizeof(*address)) != 0)
    return network_last_error();
  if (getsockname(socket_value, (struct sockaddr *)address,
                  &address_length) != 0)
    return network_last_error();
  return TURBO_OK;
}

static int network_make_tcp_pair(network_fixture *fixture) {
  network_socket listener = NETWORK_INVALID_SOCKET;
  int status;
  listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == NETWORK_INVALID_SOCKET) return network_last_error();
  status = network_bind_loopback(listener, &fixture->server_address);
  if (status == TURBO_OK && listen(listener, 1) != 0)
    status = network_last_error();
  if (status == TURBO_OK) {
    fixture->client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fixture->client_socket == NETWORK_INVALID_SOCKET)
      status = network_last_error();
  }
  if (status == TURBO_OK &&
      connect(fixture->client_socket,
              (const struct sockaddr *)&fixture->server_address,
              (int)sizeof(fixture->server_address)) != 0)
    status = network_last_error();
  if (status == TURBO_OK) {
    fixture->server_socket = accept(listener, NULL, NULL);
    if (fixture->server_socket == NETWORK_INVALID_SOCKET)
      status = network_last_error();
  }
  network_close(listener);
  if (status == TURBO_OK)
    status = network_set_nonblocking(fixture->client_socket);
  return status;
}

static int network_make_udp_pair(network_fixture *fixture) {
  struct sockaddr_in client_address;
  int status;
  fixture->client_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  fixture->server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fixture->client_socket == NETWORK_INVALID_SOCKET ||
      fixture->server_socket == NETWORK_INVALID_SOCKET)
    return network_last_error();
  status = network_bind_loopback(fixture->client_socket, &client_address);
  if (status == TURBO_OK)
    status = network_bind_loopback(fixture->server_socket,
                                   &fixture->server_address);
  if (status == TURBO_OK)
    status = network_set_nonblocking(fixture->client_socket);
  return status;
}

static int network_recv_exact(network_socket socket_value,
                              unsigned char *buffer, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    int count = recv(socket_value, (char *)buffer + offset,
                     (int)(size - offset), 0);
    if (count == 0) return TURBO_EOF;
    if (count < 0) return network_last_error();
    offset += (size_t)count;
  }
  return TURBO_OK;
}

static int network_send_exact(network_socket socket_value,
                              const unsigned char *buffer, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    int count = send(socket_value, (const char *)buffer + offset,
                     (int)(size - offset), 0);
    if (count <= 0) return network_last_error();
    offset += (size_t)count;
  }
  return TURBO_OK;
}

static void network_server_entry(void *user) {
  network_server *server = (network_server *)user;
  size_t exchange;
  server->status = TURBO_OK;
  for (exchange = 0u; exchange < server->exchanges; ++exchange) {
    if (server->protocol == NETWORK_PROTOCOL_TCP) {
      server->status = network_recv_exact(server->socket_value,
                                          server->buffer,
                                          server->payload_size);
      if (server->status == TURBO_OK)
        server->status = network_send_exact(server->socket_value,
                                            server->buffer,
                                            server->payload_size);
    } else {
      struct sockaddr_storage source;
#if defined(_WIN32)
      int source_length = (int)sizeof(source);
#else
      socklen_t source_length = (socklen_t)sizeof(source);
#endif
      int count = recvfrom(server->socket_value, (char *)server->buffer,
                           (int)server->payload_size, 0,
                           (struct sockaddr *)&source, &source_length);
      if (count != (int)server->payload_size) {
        server->status = count < 0 ? network_last_error() : TURBO_EIO;
      } else {
        count = sendto(server->socket_value, (const char *)server->buffer,
                       count, 0, (const struct sockaddr *)&source,
                       source_length);
        if (count != (int)server->payload_size)
          server->status = count < 0 ? network_last_error() : TURBO_EIO;
      }
    }
    if (server->status != TURBO_OK) return;
  }
}

static void network_operation_release(void *user) {
  free(user);
}

static void network_completion_record(
    void *user, cflow_io_request_id request_id,
    cflow_io_lease_id lease_id, void *operation_user,
    const cflow_io_completion *completion) {
  network_completion_probe *probe = (network_completion_probe *)user;
  (void)lease_id;
  (void)operation_user;
  if (probe->count < NETWORK_REQUEST_CAPACITY) {
    probe->ids[probe->count] = request_id;
    probe->values[probe->count] = *completion;
    ++probe->count;
  }
}

static int network_select_backend(cflow_io_native_backend_kind *out) {
  const char *requested = getenv("CFLOW_NETWORK_BACKEND");
  if (out == NULL) return TURBO_EINVAL;
  if (requested != NULL) {
    if (strcmp(requested, "epoll") == 0)
      *out = CFLOW_IO_NATIVE_EPOLL;
    else if (strcmp(requested, "kqueue") == 0)
      *out = CFLOW_IO_NATIVE_KQUEUE;
    else if (strcmp(requested, "iocp") == 0)
      *out = CFLOW_IO_NATIVE_IOCP;
    else if (strcmp(requested, "io_uring") == 0)
      *out = CFLOW_IO_NATIVE_IO_URING;
    else
      return TURBO_EINVAL;
    return cflow_io_native_backend_supported(*out) ? TURBO_OK : TURBO_ENOTSUP;
  }
#if defined(_WIN32)
  *out = CFLOW_IO_NATIVE_IOCP;
#elif defined(__APPLE__)
  *out = CFLOW_IO_NATIVE_KQUEUE;
#else
  *out = CFLOW_IO_NATIVE_EPOLL;
#endif
  return cflow_io_native_backend_supported(*out) ? TURBO_OK : TURBO_ENOTSUP;
}

static int network_parse_protocol(const char *text, network_protocol *out) {
  if (out == NULL) return TURBO_EINVAL;
  if (text == NULL || strcmp(text, "tcp") == 0) {
    *out = NETWORK_PROTOCOL_TCP;
    return TURBO_OK;
  }
  if (strcmp(text, "udp") == 0) {
    *out = NETWORK_PROTOCOL_UDP;
    return TURBO_OK;
  }
  return TURBO_EINVAL;
}

static int network_parse_profile(const char *text, bool *throughput) {
  if (throughput == NULL) return TURBO_EINVAL;
  if (text == NULL || strcmp(text, "latency") == 0) {
    *throughput = false;
    return TURBO_OK;
  }
  if (strcmp(text, "throughput") == 0) {
    *throughput = true;
    return TURBO_OK;
  }
  return TURBO_EINVAL;
}

static const char *network_backend_name(cflow_io_native_backend_kind kind) {
  switch (kind) {
    case CFLOW_IO_NATIVE_EPOLL: return "epoll";
    case CFLOW_IO_NATIVE_KQUEUE: return "kqueue";
    case CFLOW_IO_NATIVE_IOCP: return "iocp";
    case CFLOW_IO_NATIVE_IO_URING: return "io_uring";
  }
  return "unknown";
}

static int network_fixture_init(network_fixture *fixture,
                                network_protocol protocol,
                                cflow_io_native_backend_kind backend_kind,
                                size_t total_exchanges,
                                size_t payload_size) {
  cflow_io_native_backend_config backend_config = {
      backend_kind, NETWORK_REQUEST_CAPACITY, NETWORK_REQUEST_CAPACITY};
  cflow_io_actor_config actor_config;
  int status;
  memset(fixture, 0, sizeof(*fixture));
  fixture->client_socket = NETWORK_INVALID_SOCKET;
  fixture->server_socket = NETWORK_INVALID_SOCKET;
  status = cflow_io_native_backend_init(&fixture->backend, &backend_config);
  if (status != TURBO_OK) return status;
  fixture->backend_initialized = true;
  if (!cflow_executor_manual_init_with_capacity(&fixture->executor,
                                                 NETWORK_REQUEST_CAPACITY))
    return TURBO_ENOMEM;
  fixture->executor_initialized = true;
  memset(&actor_config, 0, sizeof(actor_config));
  actor_config.request_capacity = NETWORK_REQUEST_CAPACITY;
  actor_config.command_capacity = NETWORK_REQUEST_CAPACITY;
  actor_config.executor = &fixture->executor;
  actor_config.backend = cflow_io_native_backend_actor_ops();
  actor_config.backend_user = &fixture->backend;
  actor_config.completion = network_completion_record;
  actor_config.completion_user = &fixture->completions;
  status = cflow_io_actor_init(&fixture->actor, &actor_config);
  if (status != TURBO_OK) return status;
  fixture->actor_initialized = true;
  status = protocol == NETWORK_PROTOCOL_TCP
               ? network_make_tcp_pair(fixture)
               : network_make_udp_pair(fixture);
  if (status == TURBO_OK)
    status = network_set_server_timeout(fixture->server_socket);
  if (status != TURBO_OK) return status;
  fixture->server.protocol = protocol;
  fixture->server.socket_value = fixture->server_socket;
  fixture->server.exchanges = total_exchanges;
  fixture->server.payload_size = payload_size;
  fixture->server.buffer = (unsigned char *)malloc(payload_size);
  if (fixture->server.buffer == NULL) return TURBO_ENOMEM;
  status = turbo_thread_create(&fixture->server_thread,
                               network_server_entry, &fixture->server);
  if (status != TURBO_OK) return status;
  fixture->server_started = true;
  return TURBO_OK;
}

static int network_wait(network_fixture *fixture, size_t completion_count) {
  uint64_t started = turbo_hrtime();
  while (fixture->completions.count < completion_count) {
    (void)cflow_io_actor_run_ready(&fixture->actor, 64u);
    (void)cflow_executor_run_ready(&fixture->executor);
    if (turbo_hrtime() - started >= NETWORK_WAIT_TIMEOUT_NS)
      return TURBO_ETIMEDOUT;
    turbo_thread_yield();
  }
  return TURBO_OK;
}

static int network_run_native_operation(
    network_fixture *fixture, cflow_io_native_operation operation,
    cflow_io_lease_id lease, size_t *bytes) {
  network_operation *owned =
      (network_operation *)malloc(sizeof(*owned));
  cflow_io_operation actor_operation;
  cflow_io_submit_result submitted;
  int status;
  if (owned == NULL) return TURBO_ENOMEM;
  owned->native = operation;
  actor_operation = (cflow_io_operation){owned, network_operation_release};
  fixture->completions.count = 0u;
  submitted = cflow_io_actor_try_submit(&fixture->actor, lease,
                                        &actor_operation);
  if (submitted.status != CFLOW_IO_SUBMIT_ACCEPTED) {
    free(owned);
    return TURBO_EBUSY;
  }
  status = network_wait(fixture, 1u);
  if (status == TURBO_OK) {
    if (fixture->completions.values[0].kind != CFLOW_IO_COMPLETION_OK)
      status = fixture->completions.values[0].kind ==
                       CFLOW_IO_COMPLETION_FAILED
                   ? fixture->completions.values[0].error : TURBO_EIO;
    else
      *bytes = fixture->completions.values[0].bytes;
  }
  if (fixture->completions.count != 0u &&
      cflow_io_actor_acknowledge(
          &fixture->actor, fixture->completions.ids[0]) !=
          CFLOW_IO_ACK_RELEASED)
    status = TURBO_EIO;
  return status;
}

static int network_exchange(network_fixture *fixture,
                            network_protocol protocol,
                            unsigned char *sent,
                            unsigned char *received,
                            size_t payload_size,
                            uint64_t lease_base) {
  struct sockaddr_storage source_address;
  size_t send_offset = 0u;
  size_t receive_offset = 0u;
  size_t bytes = 0u;
  int status = TURBO_OK;
  memset(received, 0, payload_size);
  memset(&source_address, 0, sizeof(source_address));
  while (protocol == NETWORK_PROTOCOL_TCP && send_offset < payload_size) {
    cflow_io_native_operation operation = {
        CFLOW_IO_NATIVE_TCP_SEND, (uintptr_t)fixture->client_socket,
        sent + send_offset, payload_size - send_offset, NULL, 0u, 0u};
    status = network_run_native_operation(
        fixture, operation, lease_base++, &bytes);
    if (status != TURBO_OK || bytes == 0u) return TURBO_EIO;
    send_offset += bytes;
  }
  if (protocol == NETWORK_PROTOCOL_UDP) {
    cflow_io_native_operation operation = {
        CFLOW_IO_NATIVE_UDP_SEND_TO, (uintptr_t)fixture->client_socket,
        sent, payload_size, &fixture->server_address,
        sizeof(fixture->server_address), sizeof(fixture->server_address)};
    status = network_run_native_operation(
        fixture, operation, lease_base++, &bytes);
    if (status != TURBO_OK || bytes != payload_size) return TURBO_EIO;
  }
  while (protocol == NETWORK_PROTOCOL_TCP && receive_offset < payload_size) {
    cflow_io_native_operation operation = {
        CFLOW_IO_NATIVE_TCP_RECV, (uintptr_t)fixture->client_socket,
        received + receive_offset, payload_size - receive_offset,
        NULL, 0u, 0u};
    status = network_run_native_operation(
        fixture, operation, lease_base++, &bytes);
    if (status != TURBO_OK || bytes == 0u) return TURBO_EIO;
    receive_offset += bytes;
  }
  if (protocol == NETWORK_PROTOCOL_UDP) {
    cflow_io_native_operation operation = {
        CFLOW_IO_NATIVE_UDP_RECV_FROM, (uintptr_t)fixture->client_socket,
        received, payload_size, &source_address, sizeof(source_address), 0u};
    status = network_run_native_operation(
        fixture, operation, lease_base, &bytes);
    if (status != TURBO_OK || bytes != payload_size) return TURBO_EIO;
  }
  if (memcmp(sent, received, payload_size) != 0)
    status = TURBO_EIO;
  return status;
}

static void network_fixture_destroy(network_fixture *fixture) {
  if (fixture->server_started) {
    (void)turbo_thread_join(&fixture->server_thread);
    turbo_thread_destroy(&fixture->server_thread);
  }
  network_close(fixture->client_socket);
  network_close(fixture->server_socket);
  if (fixture->actor_initialized) {
    (void)cflow_io_actor_close(&fixture->actor);
    for (size_t attempt = 0u; attempt < 1024u &&
                            !cflow_io_actor_is_quiescent(&fixture->actor);
         ++attempt) {
      (void)cflow_io_actor_run_ready(&fixture->actor, 64u);
      (void)cflow_executor_run_ready(&fixture->executor);
      turbo_thread_yield();
    }
    (void)cflow_io_actor_destroy(&fixture->actor);
  }
  if (fixture->backend_initialized) {
    if (fixture->client_socket != NETWORK_INVALID_SOCKET)
      (void)cflow_io_native_backend_forget_socket(
          &fixture->backend, (uintptr_t)fixture->client_socket);
    (void)cflow_io_native_backend_shutdown(&fixture->backend);
    (void)cflow_io_native_backend_destroy(&fixture->backend);
  }
  if (fixture->executor_initialized) {
    (void)cflow_executor_shutdown(&fixture->executor);
    cflow_executor_destroy(&fixture->executor);
  }
  free(fixture->server.buffer);
}

static size_t network_env_size(const char *name, size_t fallback,
                               size_t maximum) {
  const char *text = getenv(name);
  char *end = NULL;
  unsigned long long value;
  if (text == NULL || *text == '\0') return fallback;
  value = strtoull(text, &end, 10);
  if (end == text || *end != '\0' || value == 0u || value > maximum)
    return 0u;
  return (size_t)value;
}

static uint64_t network_process_cpu_ns(void) {
#if defined(_WIN32)
  FILETIME created, exited, kernel, user;
  ULARGE_INTEGER kernel_ticks, user_ticks;
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited,
                       &kernel, &user))
    return 0u;
  kernel_ticks.LowPart = kernel.dwLowDateTime;
  kernel_ticks.HighPart = kernel.dwHighDateTime;
  user_ticks.LowPart = user.dwLowDateTime;
  user_ticks.HighPart = user.dwHighDateTime;
  return (kernel_ticks.QuadPart + user_ticks.QuadPart) * UINT64_C(100);
#else
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0u;
  return ((uint64_t)usage.ru_utime.tv_sec +
          (uint64_t)usage.ru_stime.tv_sec) * UINT64_C(1000000000) +
         ((uint64_t)usage.ru_utime.tv_usec +
          (uint64_t)usage.ru_stime.tv_usec) * UINT64_C(1000);
#endif
}

static uint64_t network_peak_rss_bytes(void) {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters;
  memset(&counters, 0, sizeof(counters));
  counters.cb = sizeof(counters);
  return GetProcessMemoryInfo(GetCurrentProcess(), &counters,
                              sizeof(counters))
             ? (uint64_t)counters.PeakWorkingSetSize : 0u;
#else
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0u;
#if defined(__APPLE__)
  return (uint64_t)usage.ru_maxrss;
#else
  return (uint64_t)usage.ru_maxrss * UINT64_C(1024);
#endif
#endif
}

static int network_u64_compare(const void *left, const void *right) {
  const uint64_t a = *(const uint64_t *)left;
  const uint64_t b = *(const uint64_t *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static uint64_t network_percentile(uint64_t *sorted, size_t count,
                                   size_t numerator) {
  size_t rank = (numerator * count + 99u) / 100u;
  if (rank == 0u) rank = 1u;
  return sorted[rank - 1u];
}

suite("CFlow native network benchmarks") {
  bench("reports loopback TCP or UDP native-backend performance") {
    const char *protocol_text = getenv("CFLOW_NETWORK_PROTOCOL");
    const char *profile_text = getenv("CFLOW_NETWORK_PROFILE");
    network_protocol protocol = NETWORK_PROTOCOL_TCP;
    bool throughput = false;
    cflow_io_native_backend_kind backend_kind = CFLOW_IO_NATIVE_EPOLL;
    int config_status = network_parse_protocol(protocol_text, &protocol);
    if (config_status == TURBO_OK)
      config_status = network_parse_profile(profile_text, &throughput);
    if (config_status == TURBO_OK)
      config_status = network_select_backend(&backend_kind);
    const size_t samples = network_env_size(
        "CFLOW_NETWORK_SAMPLES",
        throughput ? NETWORK_THROUGHPUT_SAMPLES : NETWORK_LATENCY_SAMPLES,
        NETWORK_MAX_SAMPLES);
    const size_t exchanges = network_env_size(
        "CFLOW_NETWORK_EXCHANGES",
        throughput ? NETWORK_THROUGHPUT_EXCHANGES : NETWORK_LATENCY_EXCHANGES,
        NETWORK_MAX_EXCHANGES);
    const size_t payload_size = network_env_size(
        "CFLOW_NETWORK_PAYLOAD",
        throughput
            ? (protocol == NETWORK_PROTOCOL_UDP
                   ? NETWORK_UDP_THROUGHPUT_PAYLOAD
                   : NETWORK_TCP_THROUGHPUT_PAYLOAD)
            : NETWORK_LATENCY_PAYLOAD,
        NETWORK_MAX_PAYLOAD);
    const size_t total_exchanges =
        samples != 0u && exchanges <= SIZE_MAX / samples
            ? samples * exchanges : 0u;
    const size_t bytes_per_sample =
        payload_size != 0u && exchanges <= SIZE_MAX / payload_size
            ? exchanges * payload_size : 0u;
    network_fixture fixture;
    network_measurement measured = {0};
    cflow_io_actor_stats actor_stats = {0};
    cflow_io_native_backend_stats native_stats = {0};
    unsigned char *sent = NULL;
    unsigned char *received = NULL;
    uint64_t wall_started = 0u;
    uint64_t cpu_started = 0u;
    int run_status = TURBO_OK;
    int server_join_status = TURBO_OK;
    int server_status = TURBO_OK;
    bool actor_stats_ok = false;
    bool native_stats_ok = false;
    char title[96];

    check_equal(config_status, TURBO_OK);
    check_true(samples != 0u && exchanges != 0u && payload_size != 0u &&
               total_exchanges != 0u && bytes_per_sample != 0u);
    check_true(cflow_io_native_backend_supported(backend_kind));
    measured.latencies = (uint64_t *)calloc(total_exchanges,
                                            sizeof(*measured.latencies));
    sent = (unsigned char *)malloc(payload_size);
    received = (unsigned char *)malloc(payload_size);
    check_not_null(measured.latencies);
    check_not_null(sent);
    check_not_null(received);
    measured.latency_capacity = total_exchanges;
    memset(sent, 0x5a, payload_size);
    check_equal(network_fixture_init(&fixture, protocol, backend_kind,
                                     total_exchanges, payload_size), TURBO_OK);
    (void)snprintf(title, sizeof(title), "%s-%s-%s",
                   protocol == NETWORK_PROTOCOL_TCP ? "tcp" : "udp",
                   throughput ? "throughput" : "latency",
                   network_backend_name(backend_kind));
    wall_started = turbo_hrtime();
    cpu_started = network_process_cpu_ns();
    benchmark_io(title, samples, exchanges, bytes_per_sample) {
      if (run_status == TURBO_OK) {
        for (size_t exchange = 0u; exchange < exchanges; ++exchange) {
          const uint64_t started = turbo_hrtime();
          run_status = network_exchange(
              &fixture, protocol, sent, received, payload_size,
              (cflow_io_lease_id)(measured.latency_count * 2u + 1u));
          if (run_status != TURBO_OK) break;
          measured.latencies[measured.latency_count++] =
              turbo_hrtime() - started;
        }
      }
    }
    measured.wall_ns = turbo_hrtime() - wall_started;
    measured.cpu_ns = network_process_cpu_ns() - cpu_started;
    measured.peak_rss_bytes = network_peak_rss_bytes();
    if (fixture.server_started) {
      server_join_status = turbo_thread_join(&fixture.server_thread);
      if (server_join_status == TURBO_OK) {
        turbo_thread_destroy(&fixture.server_thread);
        fixture.server_started = false;
      }
    }
    server_status = fixture.server.status;
    actor_stats_ok = cflow_io_actor_get_stats(&fixture.actor, &actor_stats);
    native_stats_ok = cflow_io_native_backend_get_stats(&fixture.backend,
                                                        &native_stats);
    if (server_join_status == TURBO_OK && server_status == TURBO_OK &&
        run_status == TURBO_OK &&
        measured.latency_count == total_exchanges && actor_stats_ok &&
        native_stats_ok) {
      qsort(measured.latencies, measured.latency_count,
            sizeof(*measured.latencies), network_u64_compare);
      printf("CFLOW_BENCHMARK_JSON {\"schema\":\"cflow-network-benchmark/v1\","
           "\"protocol\":\"%s\",\"profile\":\"%s\",\"backend\":\"%s\","
           "\"samples\":%zu,\"exchanges_per_sample\":%zu,"
           "\"payload_bytes\":%zu,\"application_bytes\":%" PRIu64 ","
           "\"wire_bytes\":%" PRIu64 ",\"wall_ns\":%" PRIu64 ","
           "\"process_cpu_ns\":%" PRIu64 ",\"process_cpu_pct\":%.3f,"
           "\"peak_rss_bytes\":%" PRIu64 ",\"p50_ns\":%" PRIu64 ","
           "\"p95_ns\":%" PRIu64 ",\"p99_ns\":%" PRIu64 ","
           "\"attempted\":%zu,\"errors\":0,\"rejections\":%" PRIu64 ","
           "\"stale_completions\":%" PRIu64 "}\n",
           protocol == NETWORK_PROTOCOL_TCP ? "tcp" : "udp",
           throughput ? "throughput" : "latency",
           network_backend_name(backend_kind), samples, exchanges, payload_size,
           (uint64_t)total_exchanges * (uint64_t)payload_size,
           (uint64_t)total_exchanges * (uint64_t)payload_size * UINT64_C(2),
           measured.wall_ns, measured.cpu_ns,
           measured.wall_ns != 0u
               ? (double)measured.cpu_ns * 100.0 / (double)measured.wall_ns
               : 0.0,
           measured.peak_rss_bytes,
           network_percentile(measured.latencies, measured.latency_count, 50u),
           network_percentile(measured.latencies, measured.latency_count, 95u),
           network_percentile(measured.latencies, measured.latency_count, 99u),
           total_exchanges,
           actor_stats.rejected_request_full + actor_stats.rejected_command_full +
               actor_stats.rejected_closed + actor_stats.rejected_lease_in_use +
               actor_stats.executor_rejected_full +
               actor_stats.executor_rejected_closed +
               actor_stats.executor_rejected_invalid + native_stats.rejected_full,
             actor_stats.stale_completions +
                 native_stats.stale_native_completions);
    }
    network_fixture_destroy(&fixture);
    free(received);
    free(sent);
    free(measured.latencies);
    check_equal(server_join_status, TURBO_OK);
    check_equal(server_status, TURBO_OK);
    check_equal(run_status, TURBO_OK);
    check_equal(measured.latency_count, total_exchanges);
    check_true(actor_stats_ok);
    check_true(native_stats_ok);
  }
}
