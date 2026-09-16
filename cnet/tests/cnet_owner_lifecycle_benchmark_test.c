#if !defined(_WIN32) && !defined(_GNU_SOURCE)
  #define _GNU_SOURCE
#endif

#include <cnet/cnet.h>
#include <salts/clock.h>
#include <salts/thread.h>

#include "cnet_benchmark_stats.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <Windows.h>
  #include <direct.h>
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET lifecycle_socket;
typedef int lifecycle_socklen;
typedef HMODULE lifecycle_library;
  #define LIFECYCLE_INVALID_SOCKET INVALID_SOCKET
#else
  #include <dlfcn.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <sys/socket.h>
  #include <sys/stat.h>
  #include <sys/time.h>
  #include <unistd.h>
typedef int lifecycle_socket;
typedef socklen_t lifecycle_socklen;
typedef void *lifecycle_library;
  #define LIFECYCLE_INVALID_SOCKET (-1)
#endif

enum {
  LIFECYCLE_REPEATS = 5,
  LIFECYCLE_WARMUPS = 32,
  LIFECYCLE_EXCHANGES = 512,
  LIFECYCLE_TOTAL_EXCHANGES = LIFECYCLE_WARMUPS + LIFECYCLE_EXCHANGES,
  LIFECYCLE_PAYLOAD_BYTES = 1024,
  LIFECYCLE_TIMEOUT_MS = 5000
};

typedef int (*lifecycle_client_init_fn)(cnet_client *, const cnet_client_config *);
typedef int (*lifecycle_connect_fn)(cnet_client *, const cnet_connect_options *, cnet_connection *);
typedef int (*lifecycle_send_fn)(cnet_client *, cnet_connection, const void *, size_t);
typedef int (*lifecycle_receive_fn)(cnet_client *, cnet_connection, size_t);
typedef int (*lifecycle_poll_fn)(cnet_client *, uint32_t, size_t *);
typedef int (*lifecycle_close_fn)(cnet_client *, cnet_connection);
typedef int (*lifecycle_stop_fn)(cnet_client *, uint32_t);
typedef int (*lifecycle_destroy_fn)(cnet_client *);

typedef struct lifecycle_api {
  lifecycle_library library;
  lifecycle_client_init_fn client_init;
  lifecycle_connect_fn connect;
  lifecycle_send_fn send;
  lifecycle_receive_fn receive;
  lifecycle_poll_fn poll;
  lifecycle_close_fn close;
  lifecycle_stop_fn stop;
  lifecycle_destroy_fn destroy;
} lifecycle_api;

typedef struct lifecycle_server {
  lifecycle_socket listener;
  uint16_t port;
  salts_thread_t thread;
  int status;
  int thread_started;
} lifecycle_server;

typedef struct lifecycle_client_probe {
  const lifecycle_api *api;
  cnet_client client;
  cnet_connection connection;
  const unsigned char *expected;
  size_t expected_size;
  size_t received;
  int connected;
  int done;
  int terminal;
  int status;
} lifecycle_client_probe;

typedef struct lifecycle_result {
  uint64_t p50_ns;
  uint64_t p95_ns;
  uint64_t wall_ns;
  double rate_per_second;
} lifecycle_result;

static int lifecycle_socket_error(void) {
#ifdef _WIN32
  const int value = WSAGetLastError();
#else
  const int value = errno;
#endif
  return value == 0 ? SALTS_EIO : -value;
}

static int lifecycle_socket_valid(lifecycle_socket value) {
  return value != LIFECYCLE_INVALID_SOCKET;
}

static void lifecycle_socket_close(lifecycle_socket value) {
  if (!lifecycle_socket_valid(value)) return;
#ifdef _WIN32
  (void)closesocket(value);
#else
  (void)close(value);
#endif
}

static int lifecycle_set_timeout(lifecycle_socket value) {
#ifdef _WIN32
  const DWORD timeout = LIFECYCLE_TIMEOUT_MS;
  if (setsockopt(value, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, (int)sizeof(timeout)) != 0)
    return lifecycle_socket_error();
  if (setsockopt(value, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, (int)sizeof(timeout)) != 0)
    return lifecycle_socket_error();
#else
  const struct timeval timeout = {LIFECYCLE_TIMEOUT_MS / 1000,
                                  (LIFECYCLE_TIMEOUT_MS % 1000) * 1000};
  if (setsockopt(value, SOL_SOCKET, SO_RCVTIMEO, &timeout, (socklen_t)sizeof(timeout)) != 0)
    return lifecycle_socket_error();
  if (setsockopt(value, SOL_SOCKET, SO_SNDTIMEO, &timeout, (socklen_t)sizeof(timeout)) != 0)
    return lifecycle_socket_error();
#endif
  return SALTS_OK;
}

static int lifecycle_set_nodelay(lifecycle_socket value) {
  const int enabled = 1;
#ifdef _WIN32
  return setsockopt(value, IPPROTO_TCP, TCP_NODELAY, (const char *)&enabled,
                    (int)sizeof(enabled)) == 0
             ? SALTS_OK
             : lifecycle_socket_error();
#else
  return setsockopt(value, IPPROTO_TCP, TCP_NODELAY, &enabled, (socklen_t)sizeof(enabled)) == 0
             ? SALTS_OK
             : lifecycle_socket_error();
#endif
}

static int lifecycle_disable_sigpipe(lifecycle_socket value) {
#if defined(SO_NOSIGPIPE)
  const int enabled = 1;
  return setsockopt(value, SOL_SOCKET, SO_NOSIGPIPE, &enabled, (socklen_t)sizeof(enabled)) == 0
             ? SALTS_OK
             : lifecycle_socket_error();
#else
  (void)value;
  return SALTS_OK;
#endif
}

static int lifecycle_recv_all(lifecycle_socket value, unsigned char *buffer, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
#ifdef _WIN32
    const int received = recv(value, (char *)buffer + offset, (int)(size - offset), 0);
#else
    const ssize_t received = recv(value, buffer + offset, size - offset, 0);
#endif
    if (received <= 0) return received < 0 ? lifecycle_socket_error() : SALTS_EIO;
    offset += (size_t)received;
  }
  return SALTS_OK;
}

static int lifecycle_send_all(lifecycle_socket value, const unsigned char *buffer, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
#ifdef _WIN32
    const int sent = send(value, (const char *)buffer + offset, (int)(size - offset), 0);
#else
    const int flags =
  #if defined(MSG_NOSIGNAL)
        MSG_NOSIGNAL;
  #else
        0;
  #endif
    const ssize_t sent = send(value, buffer + offset, size - offset, flags);
#endif
    if (sent <= 0) return sent < 0 ? lifecycle_socket_error() : SALTS_EIO;
    offset += (size_t)sent;
  }
  return SALTS_OK;
}

static void lifecycle_server_entry(void *argument) {
  lifecycle_server *server = (lifecycle_server *)argument;
  lifecycle_socket active = LIFECYCLE_INVALID_SOCKET;
  unsigned char buffer[LIFECYCLE_PAYLOAD_BYTES];
  int status = SALTS_OK;

  active = accept(server->listener, NULL, NULL);
  if (!lifecycle_socket_valid(active)) status = lifecycle_socket_error();
  if (status == SALTS_OK) status = lifecycle_set_timeout(active);
  if (status == SALTS_OK) status = lifecycle_set_nodelay(active);
  if (status == SALTS_OK) status = lifecycle_disable_sigpipe(active);
  for (size_t index = 0u; status == SALTS_OK && index < LIFECYCLE_TOTAL_EXCHANGES; ++index) {
    status = lifecycle_recv_all(active, buffer, sizeof(buffer));
    if (status == SALTS_OK) status = lifecycle_send_all(active, buffer, sizeof(buffer));
  }
  lifecycle_socket_close(active);
  server->status = status;
}

static int lifecycle_server_start(lifecycle_server *server) {
  struct sockaddr_in address;
  lifecycle_socklen address_length = (lifecycle_socklen)sizeof(address);
  int status;
  memset(server, 0, sizeof(*server));
  server->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (!lifecycle_socket_valid(server->listener)) return lifecycle_socket_error();
  status = lifecycle_set_timeout(server->listener);
  if (status == SALTS_OK) status = lifecycle_disable_sigpipe(server->listener);
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0u;
  if (status == SALTS_OK &&
      bind(server->listener, (const struct sockaddr *)&address, (int)sizeof(address)) != 0)
    status = lifecycle_socket_error();
  if (status == SALTS_OK &&
      getsockname(server->listener, (struct sockaddr *)&address, &address_length) != 0)
    status = lifecycle_socket_error();
  if (status == SALTS_OK && listen(server->listener, 1) != 0) status = lifecycle_socket_error();
  if (status == SALTS_OK) {
    server->port = ntohs(address.sin_port);
    server->status = SALTS_OK;
    status = salts_thread_create(&server->thread, lifecycle_server_entry, server);
    if (status == SALTS_OK) server->thread_started = 1;
  }
  if (status != SALTS_OK) {
    lifecycle_socket_close(server->listener);
    server->listener = LIFECYCLE_INVALID_SOCKET;
  }
  return status;
}

static int lifecycle_server_finish(lifecycle_server *server) {
  int status = SALTS_OK;
  if (server->thread_started) {
    status = salts_thread_join(&server->thread);
    salts_thread_destroy(&server->thread);
    server->thread_started = 0;
    if (status == SALTS_OK) status = server->status;
  }
  lifecycle_socket_close(server->listener);
  server->listener = LIFECYCLE_INVALID_SOCKET;
  return status;
}

static lifecycle_library lifecycle_library_open(const char *path) {
#ifdef _WIN32
  return LoadLibraryA(path);
#else
  return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void lifecycle_library_close(lifecycle_library library) {
#ifdef _WIN32
  if (library != NULL) (void)FreeLibrary(library);
#else
  if (library != NULL) (void)dlclose(library);
#endif
}

static void *lifecycle_library_symbol(lifecycle_library library, const char *name) {
#ifdef _WIN32
  FARPROC symbol = GetProcAddress(library, name);
  return (void *)(uintptr_t)symbol;
#else
  return dlsym(library, name);
#endif
}

static int lifecycle_load_function(lifecycle_library library, const char *name, void *out,
                                   size_t out_size) {
  void *symbol = lifecycle_library_symbol(library, name);
  if (symbol == NULL || out == NULL || out_size != sizeof(symbol)) return SALTS_ENOENT;
  memcpy(out, &symbol, out_size);
  return SALTS_OK;
}

static int lifecycle_api_open(const char *path, lifecycle_api *api) {
  int status;
  memset(api, 0, sizeof(*api));
  api->library = lifecycle_library_open(path);
  if (api->library == NULL) return SALTS_ENOENT;
#define LIFECYCLE_LOAD(field, symbol_name)                                                         \
  do {                                                                                             \
    status = lifecycle_load_function(api->library, symbol_name, &api->field, sizeof(api->field));  \
    if (status != SALTS_OK) goto fail;                                                             \
  } while (0)
  LIFECYCLE_LOAD(client_init, "cnet_client_init");
  LIFECYCLE_LOAD(connect, "cnet_connect");
  LIFECYCLE_LOAD(send, "cnet_send");
  LIFECYCLE_LOAD(receive, "cnet_receive");
  LIFECYCLE_LOAD(poll, "cnet_client_poll");
  LIFECYCLE_LOAD(close, "cnet_close");
  LIFECYCLE_LOAD(stop, "cnet_client_stop");
  LIFECYCLE_LOAD(destroy, "cnet_client_destroy");
#undef LIFECYCLE_LOAD
  return SALTS_OK;

fail:
  lifecycle_library_close(api->library);
  memset(api, 0, sizeof(*api));
  return status;
}

static void lifecycle_api_close(lifecycle_api *api) {
  lifecycle_library_close(api->library);
  memset(api, 0, sizeof(*api));
}

static native_io_backend_kind lifecycle_backend_kind(void) {
  const char *requested = getenv("CNET_IO_BENCHMARK_BACKEND");
  if (requested != NULL) {
#ifdef _WIN32
    if (strcmp(requested, "iocp") == 0) return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
    if (strcmp(requested, "io_uring") == 0) return NATIVE_IO_BACKEND_IO_URING;
    if (strcmp(requested, "epoll") == 0) return NATIVE_IO_BACKEND_EPOLL;
#else
    if (strcmp(requested, "kqueue") == 0) return NATIVE_IO_BACKEND_KQUEUE;
#endif
  }
#ifdef _WIN32
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
  return NATIVE_IO_BACKEND_EPOLL;
#else
  return NATIVE_IO_BACKEND_KQUEUE;
#endif
}

static const char *lifecycle_backend_name(native_io_backend_kind kind) {
  switch (kind) {
  case NATIVE_IO_BACKEND_IOCP:
    return "iocp";
  case NATIVE_IO_BACKEND_EPOLL:
    return "epoll";
  case NATIVE_IO_BACKEND_IO_URING:
    return "io_uring";
  case NATIVE_IO_BACKEND_KQUEUE:
    return "kqueue";
  default:
    return "unknown";
  }
}

static void lifecycle_state(void *user, cnet_connection connection, cnet_connection_state state,
                            const cnet_error *error) {
  lifecycle_client_probe *probe = (lifecycle_client_probe *)user;
  (void)connection;
  if (state == CNET_CONNECTION_CONNECTED) probe->connected = 1;
  else if (state == CNET_CONNECTION_CLOSED) probe->terminal = 1;
  else if (state == CNET_CONNECTION_FAILED) {
    probe->status = error == NULL ? SALTS_EIO : error->status;
    probe->terminal = 1;
    probe->done = 1;
  }
}

static void lifecycle_receive(void *user, cnet_connection connection, const cnet_receive_view *view) {
  lifecycle_client_probe *probe = (lifecycle_client_probe *)user;
  (void)connection;
  if (view == NULL || view->kind != CNET_MESSAGE_BYTES ||
      view->size > probe->expected_size - probe->received ||
      memcmp(probe->expected + probe->received, view->data, view->size) != 0) {
    probe->status = SALTS_EIO;
    probe->done = 1;
    return;
  }
  probe->received += view->size;
  if (probe->received == probe->expected_size) probe->done = 1;
}

static int lifecycle_poll_until(lifecycle_client_probe *probe, int *value, int expected) {
  const uint64_t deadline = salts_monotonic_ms() + LIFECYCLE_TIMEOUT_MS;
  while (*value != expected) {
    size_t events = 0u;
    int status = probe->api->poll(&probe->client, 1u, &events);
    if (status != SALTS_OK) return status;
    if (probe->status != SALTS_OK) return probe->status;
    if (salts_monotonic_ms() >= deadline) return SALTS_ETIMEDOUT;
  }
  return SALTS_OK;
}

static int lifecycle_client_start(const lifecycle_api *api, uint16_t port,
                                  lifecycle_client_probe *probe) {
  const native_io_backend_kind backend = lifecycle_backend_kind();
  const cnet_client_config config = {.backend = backend,
                                     .connection_capacity = 1u,
                                     .command_capacity = 8u,
                                     .request_capacity = 4u,
                                     .completion_batch_capacity = 4u,
                                     .event_capacity = 8u,
                                     .max_send_bytes = 65536u,
                                     .receive_buffer_bytes = 65536u,
                                     .connect_timeout_ms = LIFECYCLE_TIMEOUT_MS,
                                     .read_timeout_ms = 0u,
                                     .write_timeout_ms = 0u};
  cnet_connect_options options;
  char uri[64];
  int status;
  memset(probe, 0, sizeof(*probe));
  probe->api = api;
  probe->status = SALTS_OK;
  status = api->client_init(&probe->client, &config);
  if (status != SALTS_OK) return status;
  (void)snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)port);
  options = (cnet_connect_options){.uri = uri,
                                   .observer = {.on_state = lifecycle_state,
                                                .on_receive = lifecycle_receive,
                                                .user = probe}};
  status = api->connect(&probe->client, &options, &probe->connection);
  if (status == SALTS_OK) status = lifecycle_poll_until(probe, &probe->connected, 1);
  if (status == SALTS_OK)
    status = api->receive(&probe->client, probe->connection,
                          (size_t)LIFECYCLE_PAYLOAD_BYTES * LIFECYCLE_TOTAL_EXCHANGES);
  return status;
}

static int lifecycle_client_exchange(lifecycle_client_probe *probe,
                                     const unsigned char payload[LIFECYCLE_PAYLOAD_BYTES]) {
  int status;
  probe->expected = payload;
  probe->expected_size = LIFECYCLE_PAYLOAD_BYTES;
  probe->received = 0u;
  probe->done = 0;
  probe->status = SALTS_OK;
  status = probe->api->send(&probe->client, probe->connection, payload, LIFECYCLE_PAYLOAD_BYTES);
  if (status == SALTS_OK) status = lifecycle_poll_until(probe, &probe->done, 1);
  return status == SALTS_OK ? probe->status : status;
}

static int lifecycle_client_finish(lifecycle_client_probe *probe) {
  int status = SALTS_OK;
  if (probe->client.impl == NULL) return SALTS_OK;
  if (!probe->terminal) status = probe->api->close(&probe->client, probe->connection);
  if (status == SALTS_OK && !probe->terminal)
    status = lifecycle_poll_until(probe, &probe->terminal, 1);
  if (status == SALTS_EALREADY || status == SALTS_ENOENT) status = SALTS_OK;
  if (status == SALTS_OK) status = probe->api->stop(&probe->client, LIFECYCLE_TIMEOUT_MS);
  if (status == SALTS_OK) status = probe->api->destroy(&probe->client);
  return status;
}

static int lifecycle_u64_compare(const void *left, const void *right) {
  const uint64_t lhs = *(const uint64_t *)left;
  const uint64_t rhs = *(const uint64_t *)right;
  return lhs < rhs ? -1 : lhs > rhs;
}

static int lifecycle_run_sample(const char *library_path, lifecycle_result *result) {
  lifecycle_api api;
  lifecycle_server server;
  lifecycle_client_probe probe;
  unsigned char payload[LIFECYCLE_PAYLOAD_BYTES];
  uint64_t latencies[LIFECYCLE_EXCHANGES];
  uint64_t wall_started = 0u;
  int status;

  memset(result, 0, sizeof(*result));
  memset(&server, 0, sizeof(server));
  server.listener = LIFECYCLE_INVALID_SOCKET;
  memset(payload, 0x5a, sizeof(payload));

  status = lifecycle_api_open(library_path, &api);
  if (status != SALTS_OK) return status;
  status = lifecycle_server_start(&server);
  if (status == SALTS_OK) status = lifecycle_client_start(&api, server.port, &probe);
  for (size_t index = 0u; status == SALTS_OK && index < LIFECYCLE_WARMUPS; ++index)
    status = lifecycle_client_exchange(&probe, payload);
  if (status == SALTS_OK) wall_started = salts_hrtime();
  for (size_t index = 0u; status == SALTS_OK && index < LIFECYCLE_EXCHANGES; ++index) {
    const uint64_t started = salts_hrtime();
    status = lifecycle_client_exchange(&probe, payload);
    latencies[index] = salts_hrtime() - started;
  }
  if (status == SALTS_OK) {
    result->wall_ns = salts_hrtime() - wall_started;
    qsort(latencies, LIFECYCLE_EXCHANGES, sizeof(latencies[0]), lifecycle_u64_compare);
    result->p50_ns = latencies[(LIFECYCLE_EXCHANGES - 1u) * 50u / 100u];
    result->p95_ns = latencies[(LIFECYCLE_EXCHANGES - 1u) * 95u / 100u];
    result->rate_per_second = (double)LIFECYCLE_EXCHANGES * 1000000000.0 / (double)result->wall_ns;
  }
  {
    const int client_status = lifecycle_client_finish(&probe);
    if (status == SALTS_OK) status = client_status;
  }
  {
    const int server_status = lifecycle_server_finish(&server);
    if (status == SALTS_OK) status = server_status;
  }
  lifecycle_api_close(&api);
  return status;
}

static int lifecycle_make_parent(const char *path) {
  char *copy;
  char *slash;
  int status = SALTS_OK;
  if (path == NULL) return SALTS_EINVAL;
  copy = (char *)malloc(strlen(path) + 1u);
  if (copy == NULL) return SALTS_ENOMEM;
  memcpy(copy, path, strlen(path) + 1u);
  slash = strrchr(copy, '/');
#ifdef _WIN32
  {
    char *backslash = strrchr(copy, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
  }
#endif
  if (slash != NULL) {
    *slash = '\0';
    if (copy[0] != '\0') {
#ifdef _WIN32
      if (_mkdir(copy) != 0 && errno != EEXIST) status = SALTS_EIO;
#else
      if (mkdir(copy, 0777) != 0 && errno != EEXIST) status = SALTS_EIO;
#endif
    }
  }
  free(copy);
  return status;
}

static int lifecycle_write_report(const char *path, const lifecycle_result *coroutine,
                                  const lifecycle_result *direct) {
  double coroutine_p50[LIFECYCLE_REPEATS];
  double direct_p50[LIFECYCLE_REPEATS];
  double coroutine_p95[LIFECYCLE_REPEATS];
  double direct_p95[LIFECYCLE_REPEATS];
  double coroutine_rate[LIFECYCLE_REPEATS];
  double direct_rate[LIFECYCLE_REPEATS];
  cnet_benchmark_summary p50_delta = {0};
  cnet_benchmark_summary p95_delta = {0};
  cnet_benchmark_summary rate_delta = {0};
  cnet_benchmark_summary coroutine_p50_summary = {0};
  cnet_benchmark_summary direct_p50_summary = {0};
  FILE *stream;
  int status;

  for (size_t index = 0u; index < LIFECYCLE_REPEATS; ++index) {
    coroutine_p50[index] = (double)coroutine[index].p50_ns;
    direct_p50[index] = (double)direct[index].p50_ns;
    coroutine_p95[index] = (double)coroutine[index].p95_ns;
    direct_p95[index] = (double)direct[index].p95_ns;
    coroutine_rate[index] = coroutine[index].rate_per_second;
    direct_rate[index] = direct[index].rate_per_second;
  }
  status = cnet_benchmark_summarize_paired_delta(coroutine_p50, direct_p50, LIFECYCLE_REPEATS,
                                                  &p50_delta);
  if (status == SALTS_OK)
    status = cnet_benchmark_summarize_paired_delta(coroutine_p95, direct_p95, LIFECYCLE_REPEATS,
                                                    &p95_delta);
  if (status == SALTS_OK)
    status = cnet_benchmark_summarize_paired_delta(coroutine_rate, direct_rate, LIFECYCLE_REPEATS,
                                                    &rate_delta);
  if (status == SALTS_OK)
    status = cnet_benchmark_summarize(coroutine_p50, LIFECYCLE_REPEATS, &coroutine_p50_summary);
  if (status == SALTS_OK)
    status = cnet_benchmark_summarize(direct_p50, LIFECYCLE_REPEATS, &direct_p50_summary);
  if (status != SALTS_OK) return status;

  status = lifecycle_make_parent(path);
  if (status != SALTS_OK) return status;
  stream = fopen(path, "w");
  if (stream == NULL) return SALTS_EIO;
  fprintf(stream, "# CNet owner lifecycle benchmark\n\n");
  fprintf(stream, "Backend: `%s`\n\n", lifecycle_backend_name(lifecycle_backend_kind()));
  fprintf(stream,
          "Protocol: TCP, payload 1 KiB, %d paired repeats, %d warmups + %d measured persistent "
          "round trips per sample. B0 is current per-I/O coroutine owner; B1 is direct NativeIO "
          "submit/observe/cancel. Variant order alternates inside each pair.\n\n",
          LIFECYCLE_REPEATS, LIFECYCLE_WARMUPS, LIFECYCLE_EXCHANGES);
  fprintf(stream, "Comparison rows are uninstrumented. DSO load/unload and fixture construction are "
                  "outside the measured window.\n\n");
  fprintf(stream, "## Paired result\n\n");
  fprintf(stream, "| metric | B1 direct vs B0 coroutine median | MAD |\n");
  fprintf(stream, "| --- | ---: | ---: |\n");
  fprintf(stream, "| p50 latency | %+.2f%% | %.2fpp |\n", p50_delta.median, p50_delta.mad);
  fprintf(stream, "| p95 latency | %+.2f%% | %.2fpp |\n", p95_delta.median, p95_delta.mad);
  fprintf(stream, "| rate | %+.2f%% | %.2fpp |\n\n", rate_delta.median, rate_delta.mad);
  fprintf(stream, "B0 p50 median/MAD: %.3f / %.3f us.  B1 p50 median/MAD: %.3f / %.3f us.\n\n",
          coroutine_p50_summary.median / 1000.0, coroutine_p50_summary.mad / 1000.0,
          direct_p50_summary.median / 1000.0, direct_p50_summary.mad / 1000.0);
  fprintf(stream, "## Raw paired repeats\n\n");
  fprintf(stream, "| repeat | first | B0 p50 us | B1 p50 us | B0 p95 us | B1 p95 us | B0 RT/s | B1 RT/s |\n");
  fprintf(stream, "| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < LIFECYCLE_REPEATS; ++index) {
    fprintf(stream, "| %zu | %s | %.3f | %.3f | %.3f | %.3f | %.0f | %.0f |\n", index + 1u,
            (index & 1u) == 0u ? "B0" : "B1", (double)coroutine[index].p50_ns / 1000.0,
            (double)direct[index].p50_ns / 1000.0, (double)coroutine[index].p95_ns / 1000.0,
            (double)direct[index].p95_ns / 1000.0, coroutine[index].rate_per_second,
            direct[index].rate_per_second);
  }
  fprintf(stream,
          "\nDecision is intentionally deferred until this paired result is compared with the "
          "NativeIO direct A/A p50 noise envelope from `libuv-native-io-cnet-benchmark.md` in the "
          "same exact-head artifact.\n");
  if (fclose(stream) != 0) return SALTS_EIO;
  return SALTS_OK;
}

int main(int argc, char **argv) {
  lifecycle_result coroutine[LIFECYCLE_REPEATS] = {{0}};
  lifecycle_result direct[LIFECYCLE_REPEATS] = {{0}};
  int status = SALTS_OK;

  if (argc != 4) {
    fprintf(stderr, "usage: %s <B0-cnet-library> <B1-direct-library> <report-path>\n", argv[0]);
    return 2;
  }
#ifdef _WIN32
  {
    WSADATA data;
    const int wsa_status = WSAStartup(MAKEWORD(2, 2), &data);
    if (wsa_status != 0) return wsa_status;
  }
#endif

  for (size_t repeat = 0u; status == SALTS_OK && repeat < LIFECYCLE_REPEATS; ++repeat) {
    const int direct_first = (repeat & 1u) != 0u;
    if (direct_first) {
      status = lifecycle_run_sample(argv[2], &direct[repeat]);
      if (status == SALTS_OK) status = lifecycle_run_sample(argv[1], &coroutine[repeat]);
    } else {
      status = lifecycle_run_sample(argv[1], &coroutine[repeat]);
      if (status == SALTS_OK) status = lifecycle_run_sample(argv[2], &direct[repeat]);
    }
  }
  if (status == SALTS_OK) status = lifecycle_write_report(argv[3], coroutine, direct);

#ifdef _WIN32
  (void)WSACleanup();
#endif
  if (status != SALTS_OK)
    fprintf(stderr, "CNet owner lifecycle benchmark failed: status=%d\n", status);
  return status == SALTS_OK ? 0 : 1;
}
