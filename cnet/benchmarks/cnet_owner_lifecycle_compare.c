#include <cnet/cnet.h>
#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/thread.h>

#include "cnet_benchmark_stats.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
typedef SOCKET compare_socket;
typedef int compare_socklen;
typedef HMODULE compare_library;
  #define COMPARE_INVALID_SOCKET INVALID_SOCKET
#else
  #include <arpa/inet.h>
  #include <dlfcn.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
typedef int compare_socket;
typedef socklen_t compare_socklen;
typedef void *compare_library;
  #define COMPARE_INVALID_SOCKET (-1)
#endif

enum {
  CNET_OWNER_COMPARE_REPEATS = 5,
  CNET_OWNER_COMPARE_WARMUPS = 32,
  CNET_OWNER_COMPARE_EXCHANGES = 512,
  CNET_OWNER_COMPARE_PAYLOAD_BYTES = 1024,
  CNET_OWNER_COMPARE_TOTAL_EXCHANGES =
      CNET_OWNER_COMPARE_WARMUPS + CNET_OWNER_COMPARE_EXCHANGES,
  CNET_OWNER_COMPARE_TIMEOUT_MS = 5000
};

typedef struct compare_api {
  compare_library library;
  int (*client_init)(cnet_client *client, const cnet_client_config *config);
  int (*connect)(cnet_client *client, const cnet_connect_options *options,
                 cnet_connection *out_connection);
  int (*send)(cnet_client *client, cnet_connection connection, const void *data, size_t size);
  int (*receive)(cnet_client *client, cnet_connection connection, size_t demand);
  int (*client_poll)(cnet_client *client, uint32_t timeout_ms, size_t *out_events);
  int (*close)(cnet_client *client, cnet_connection connection);
  int (*client_stop)(cnet_client *client, uint32_t timeout_ms);
  int (*client_destroy)(cnet_client *client);
} compare_api;

typedef struct compare_server {
  atomic_uintptr_t listener_socket;
  atomic_uintptr_t active_socket;
  atomic_bool abort_requested;
  atomic_int status;
  struct sockaddr_in address;
  salts_thread_t thread;
  bool thread_started;
} compare_server;

typedef struct compare_client {
  compare_api *api;
  cnet_client client;
  cnet_connection connection;
  const unsigned char *expected_data;
  size_t payload_size;
  size_t received;
  int connected;
  int done;
  int terminal;
  int status;
} compare_client;

typedef struct compare_result {
  uint64_t p50_ns;
  uint64_t p95_ns;
  uint64_t wall_ns;
  double rate_per_second;
} compare_result;

static bool compare_socket_valid(compare_socket socket_value) {
  return socket_value != COMPARE_INVALID_SOCKET;
}

static uintptr_t compare_socket_token(compare_socket socket_value) {
  return (uintptr_t)socket_value;
}

static compare_socket compare_socket_from_token(uintptr_t token) {
  return (compare_socket)token;
}

static int compare_socket_close(compare_socket socket_value) {
  if (!compare_socket_valid(socket_value)) return SALTS_OK;
#if defined(_WIN32)
  return closesocket(socket_value) == 0 ? SALTS_OK : SALTS_EIO;
#else
  return close(socket_value) == 0 ? SALTS_OK : SALTS_EIO;
#endif
}

static void compare_socket_shutdown(compare_socket socket_value) {
  if (!compare_socket_valid(socket_value)) return;
#if defined(_WIN32)
  (void)shutdown(socket_value, SD_BOTH);
#else
  (void)shutdown(socket_value, SHUT_RDWR);
#endif
}

static int compare_network_start(void) {
#if defined(_WIN32)
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? SALTS_OK : SALTS_EIO;
#else
  return SALTS_OK;
#endif
}

static void compare_network_stop(void) {
#if defined(_WIN32)
  (void)WSACleanup();
#endif
}

static int compare_socket_timeout(compare_socket socket_value) {
#if defined(_WIN32)
  const DWORD timeout = CNET_OWNER_COMPARE_TIMEOUT_MS;
  return setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout,
                    (int)sizeof(timeout)) == 0
             ? SALTS_OK
             : SALTS_EIO;
#else
  const struct timeval timeout = {CNET_OWNER_COMPARE_TIMEOUT_MS / 1000,
                                  (CNET_OWNER_COMPARE_TIMEOUT_MS % 1000) * 1000};
  return setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0
             ? SALTS_OK
             : SALTS_EIO;
#endif
}

static int compare_socket_no_delay(compare_socket socket_value) {
  const int enabled = 1;
#if defined(_WIN32)
  return setsockopt(socket_value, IPPROTO_TCP, TCP_NODELAY, (const char *)&enabled,
                    (int)sizeof(enabled)) == 0
             ? SALTS_OK
             : SALTS_EIO;
#else
  return setsockopt(socket_value, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) == 0
             ? SALTS_OK
             : SALTS_EIO;
#endif
}

static int compare_socket_no_sigpipe(compare_socket socket_value) {
#if defined(SO_NOSIGPIPE)
  const int enabled = 1;
  return setsockopt(socket_value, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) == 0
             ? SALTS_OK
             : SALTS_EIO;
#else
  (void)socket_value;
  return SALTS_OK;
#endif
}

static int compare_send_all(compare_socket socket_value, const unsigned char *data, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
#if defined(_WIN32)
    const int sent = send(socket_value, (const char *)data + offset, (int)(size - offset), 0);
#elif defined(MSG_NOSIGNAL)
    const ssize_t sent = send(socket_value, data + offset, size - offset, MSG_NOSIGNAL);
#else
    const ssize_t sent = send(socket_value, data + offset, size - offset, 0);
#endif
    if (sent <= 0) return SALTS_EIO;
    offset += (size_t)sent;
  }
  return SALTS_OK;
}

static int compare_receive_all(compare_socket socket_value, unsigned char *data, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
#if defined(_WIN32)
    const int received = recv(socket_value, (char *)data + offset, (int)(size - offset), 0);
#else
    const ssize_t received = recv(socket_value, data + offset, size - offset, 0);
#endif
    if (received <= 0) return SALTS_EIO;
    offset += (size_t)received;
  }
  return SALTS_OK;
}

static void compare_server_entry(void *argument) {
  compare_server *server = (compare_server *)argument;
  compare_socket listener =
      compare_socket_from_token(atomic_load_explicit(&server->listener_socket, memory_order_acquire));
  compare_socket active = COMPARE_INVALID_SOCKET;
  unsigned char payload[CNET_OWNER_COMPARE_PAYLOAD_BYTES];
  int status = SALTS_OK;

  active = accept(listener, NULL, NULL);
  if (!compare_socket_valid(active)) {
    status = atomic_load_explicit(&server->abort_requested, memory_order_acquire) ? SALTS_ECANCELED
                                                                                 : SALTS_EIO;
    goto done;
  }
  atomic_store_explicit(&server->active_socket, compare_socket_token(active), memory_order_release);
  if (atomic_load_explicit(&server->abort_requested, memory_order_acquire)) {
    compare_socket_shutdown(active);
    status = SALTS_ECANCELED;
    goto done;
  }
  status = compare_socket_timeout(active);
  if (status == SALTS_OK) status = compare_socket_no_delay(active);
  if (status == SALTS_OK) status = compare_socket_no_sigpipe(active);
  for (size_t exchange = 0u;
       status == SALTS_OK && exchange < CNET_OWNER_COMPARE_TOTAL_EXCHANGES; ++exchange) {
    status = compare_receive_all(active, payload, sizeof(payload));
    if (status == SALTS_OK) status = compare_send_all(active, payload, sizeof(payload));
  }

done:
  if (compare_socket_valid(active)) (void)compare_socket_close(active);
  atomic_store_explicit(&server->active_socket, compare_socket_token(COMPARE_INVALID_SOCKET),
                        memory_order_release);
  atomic_store_explicit(&server->status, status, memory_order_release);
}

static int compare_server_init(compare_server *server) {
  compare_socket listener = COMPARE_INVALID_SOCKET;
  compare_socklen address_size = (compare_socklen)sizeof(server->address);
  const int reuse = 1;
  int status = SALTS_OK;

  memset(server, 0, sizeof(*server));
  atomic_init(&server->listener_socket, compare_socket_token(COMPARE_INVALID_SOCKET));
  atomic_init(&server->active_socket, compare_socket_token(COMPARE_INVALID_SOCKET));
  atomic_init(&server->abort_requested, false);
  atomic_init(&server->status, SALTS_OK);
  listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (!compare_socket_valid(listener)) return SALTS_EIO;
#if defined(_WIN32)
  (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, (int)sizeof(reuse));
#else
  (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
  memset(&server->address, 0, sizeof(server->address));
  server->address.sin_family = AF_INET;
  server->address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  server->address.sin_port = 0;
  if (bind(listener, (const struct sockaddr *)&server->address, sizeof(server->address)) != 0)
    status = SALTS_EIO;
  if (status == SALTS_OK && listen(listener, 1) != 0) status = SALTS_EIO;
  if (status == SALTS_OK &&
      getsockname(listener, (struct sockaddr *)&server->address, &address_size) != 0)
    status = SALTS_EIO;
  if (status != SALTS_OK) {
    (void)compare_socket_close(listener);
    return status;
  }
  atomic_store_explicit(&server->listener_socket, compare_socket_token(listener), memory_order_release);
  return SALTS_OK;
}

static int compare_server_start(compare_server *server) {
  const int status = salts_thread_create(&server->thread, compare_server_entry, server);
  if (status == SALTS_OK) server->thread_started = true;
  return status;
}

static void compare_server_abort(compare_server *server) {
  const uintptr_t invalid = compare_socket_token(COMPARE_INVALID_SOCKET);
  const uintptr_t listener_token =
      atomic_exchange_explicit(&server->listener_socket, invalid, memory_order_acq_rel);
  const uintptr_t active_token = atomic_load_explicit(&server->active_socket, memory_order_acquire);
  atomic_store_explicit(&server->abort_requested, true, memory_order_release);
  if (listener_token != invalid) (void)compare_socket_close(compare_socket_from_token(listener_token));
  if (active_token != invalid) compare_socket_shutdown(compare_socket_from_token(active_token));
}

static int compare_server_finish(compare_server *server) {
  const uintptr_t invalid = compare_socket_token(COMPARE_INVALID_SOCKET);
  int status = SALTS_OK;
  if (server->thread_started) {
    const int join_status = salts_thread_join(&server->thread);
    salts_thread_destroy(&server->thread);
    server->thread_started = false;
    if (join_status != SALTS_OK) status = join_status;
    if (status == SALTS_OK)
      status = atomic_load_explicit(&server->status, memory_order_acquire);
  }
  {
    const uintptr_t listener_token =
        atomic_exchange_explicit(&server->listener_socket, invalid, memory_order_acq_rel);
    if (listener_token != invalid) {
      const int close_status = compare_socket_close(compare_socket_from_token(listener_token));
      if (status == SALTS_OK) status = close_status;
    }
  }
  return status;
}

static compare_library compare_library_open(const char *path) {
#if defined(_WIN32)
  return LoadLibraryA(path);
#else
  return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void compare_library_close(compare_library library) {
  if (library == NULL) return;
#if defined(_WIN32)
  (void)FreeLibrary(library);
#else
  (void)dlclose(library);
#endif
}

static bool compare_library_symbol(compare_library library, const char *name, void *out_function,
                                   size_t out_size) {
#if defined(_WIN32)
  const FARPROC symbol = GetProcAddress(library, name);
  if (symbol == NULL || sizeof(symbol) > out_size) return false;
  memset(out_function, 0, out_size);
  memcpy(out_function, &symbol, sizeof(symbol));
  return true;
#else
  void *symbol;
  const char *error;
  (void)dlerror();
  symbol = dlsym(library, name);
  error = dlerror();
  if (error != NULL || symbol == NULL || sizeof(symbol) > out_size) return false;
  memset(out_function, 0, out_size);
  memcpy(out_function, &symbol, sizeof(symbol));
  return true;
#endif
}

static int compare_api_load(compare_api *api, const char *path) {
  memset(api, 0, sizeof(*api));
  api->library = compare_library_open(path);
  if (api->library == NULL) {
    fprintf(stderr, "failed to load CNet DSO: %s\n", path);
    return SALTS_EIO;
  }
#define COMPARE_LOAD(field, symbol_name)                                                           \
  do {                                                                                             \
    if (!compare_library_symbol(api->library, symbol_name, &api->field, sizeof(api->field))) {     \
      fprintf(stderr, "missing CNet symbol %s in %s\n", symbol_name, path);                       \
      compare_library_close(api->library);                                                         \
      memset(api, 0, sizeof(*api));                                                                \
      return SALTS_ENOENT;                                                                         \
    }                                                                                              \
  } while (0)
  COMPARE_LOAD(client_init, "cnet_client_init");
  COMPARE_LOAD(connect, "cnet_connect");
  COMPARE_LOAD(send, "cnet_send");
  COMPARE_LOAD(receive, "cnet_receive");
  COMPARE_LOAD(client_poll, "cnet_client_poll");
  COMPARE_LOAD(close, "cnet_close");
  COMPARE_LOAD(client_stop, "cnet_client_stop");
  COMPARE_LOAD(client_destroy, "cnet_client_destroy");
#undef COMPARE_LOAD
  return SALTS_OK;
}

static void compare_api_unload(compare_api *api) {
  compare_library_close(api->library);
  memset(api, 0, sizeof(*api));
}

static bool compare_backend_kind(native_io_backend_kind *out_backend) {
  const char *name = getenv("CNET_IO_BENCHMARK_BACKEND");
  if (out_backend == NULL) return false;
  if (name == NULL || name[0] == '\0') {
#if defined(_WIN32)
    *out_backend = NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__)
    *out_backend = NATIVE_IO_BACKEND_KQUEUE;
#else
    *out_backend = NATIVE_IO_BACKEND_EPOLL;
#endif
    return true;
  }
  if (strcmp(name, "iocp") == 0) *out_backend = NATIVE_IO_BACKEND_IOCP;
  else if (strcmp(name, "epoll") == 0) *out_backend = NATIVE_IO_BACKEND_EPOLL;
  else if (strcmp(name, "io_uring") == 0) *out_backend = NATIVE_IO_BACKEND_IO_URING;
  else if (strcmp(name, "kqueue") == 0) *out_backend = NATIVE_IO_BACKEND_KQUEUE;
  else return false;
  return true;
}

static int compare_wait(compare_client *fixture, const int *value, int expected) {
  while (*value != expected) {
    size_t events = 0u;
    int status;
    if (fixture->status != SALTS_OK) return fixture->status;
    status = fixture->api->client_poll(&fixture->client, CNET_OWNER_COMPARE_TIMEOUT_MS, &events);
    if (status != SALTS_OK) return status;
    if (fixture->status != SALTS_OK) return fixture->status;
    if (*value != expected && events == 0u) return SALTS_ETIMEDOUT;
  }
  return SALTS_OK;
}

static void compare_cnet_state(void *user, cnet_connection connection, cnet_connection_state state,
                               const cnet_error *error) {
  compare_client *fixture = (compare_client *)user;
  (void)connection;
  if (state == CNET_CONNECTION_CONNECTED) fixture->connected = 1;
  else if (state == CNET_CONNECTION_FAILED || state == CNET_CONNECTION_CLOSED) {
    if (state == CNET_CONNECTION_FAILED)
      fixture->status = error == NULL ? SALTS_EIO : error->status;
    fixture->terminal = 1;
    fixture->done = 1;
  }
}

static void compare_cnet_receive(void *user, cnet_connection connection,
                                 const cnet_receive_view *view) {
  compare_client *fixture = (compare_client *)user;
  if (view == NULL || view->kind != CNET_MESSAGE_BYTES ||
      view->size > fixture->payload_size - fixture->received ||
      memcmp(fixture->expected_data + fixture->received, view->data, view->size) != 0) {
    fixture->status = SALTS_EIO;
    fixture->done = 1;
    return;
  }
  fixture->received += view->size;
  if (fixture->received == fixture->payload_size) fixture->done = 1;
  else {
    const int status = fixture->api->receive(&fixture->client, connection, 1u);
    if (status != SALTS_OK) {
      fixture->status = status;
      fixture->done = 1;
    }
  }
}

static int compare_client_init(compare_client *fixture, compare_api *api,
                               const struct sockaddr_in *address,
                               native_io_backend_kind backend_kind) {
  const cnet_client_config config = {.backend = backend_kind,
                                     .connection_capacity = 1u,
                                     .command_capacity = 8u,
                                     .request_capacity = 4u,
                                     .completion_batch_capacity = 4u,
                                     .event_capacity = 8u,
                                     .max_send_bytes = CNET_OWNER_COMPARE_PAYLOAD_BYTES,
                                     .write_capacity = 8u,
                                     .write_capacity_per_connection = 8u,
                                     .write_buffer_bytes = 8u * CNET_OWNER_COMPARE_PAYLOAD_BYTES,
                                     .receive_buffer_bytes = CNET_OWNER_COMPARE_PAYLOAD_BYTES,
                                     .connect_timeout_ms = CNET_OWNER_COMPARE_TIMEOUT_MS,
                                     .read_timeout_ms = 0u,
                                     .write_timeout_ms = 0u};
  cnet_connect_options options;
  char uri[64];
  int status;

  memset(fixture, 0, sizeof(*fixture));
  fixture->api = api;
  fixture->status = SALTS_OK;
  status = api->client_init(&fixture->client, &config);
  if (status != SALTS_OK) return status;
  (void)snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u", (unsigned int)ntohs(address->sin_port));
  options = (cnet_connect_options){.uri = uri,
                                   .observer = {.on_state = compare_cnet_state,
                                                .on_receive = compare_cnet_receive,
                                                .user = fixture,
                                                .on_send = NULL}};
  status = api->connect(&fixture->client, &options, &fixture->connection);
  if (status == SALTS_OK) status = compare_wait(fixture, &fixture->connected, 1);
  if (status == SALTS_OK) {
    /* Demand counts future receive callbacks, not bytes. A partial TCP chunk
       replenishes one demand from compare_cnet_receive(). */
    const size_t demand = (size_t)CNET_OWNER_COMPARE_TOTAL_EXCHANGES;
    status = api->receive(&fixture->client, fixture->connection, demand);
  }
  return status;
}

static int compare_exchange(compare_client *fixture, const unsigned char *payload) {
  int status;
  fixture->expected_data = payload;
  fixture->payload_size = CNET_OWNER_COMPARE_PAYLOAD_BYTES;
  fixture->received = 0u;
  fixture->done = 0;
  fixture->status = SALTS_OK;
  status = fixture->api->send(&fixture->client, fixture->connection, payload,
                              CNET_OWNER_COMPARE_PAYLOAD_BYTES);
  if (status == SALTS_OK) status = compare_wait(fixture, &fixture->done, 1);
  if (status == SALTS_OK) status = fixture->status;
  return status;
}

static int compare_client_destroy(compare_client *fixture) {
  int first_error = SALTS_OK;
  int status;
  if (fixture->client.impl == NULL) return SALTS_OK;
  status = fixture->api->close(&fixture->client, fixture->connection);
  if (status == SALTS_OK) status = compare_wait(fixture, &fixture->terminal, 1);
  if (status != SALTS_OK && status != SALTS_EALREADY && status != SALTS_ENOENT)
    first_error = status;
  status = fixture->api->client_stop(&fixture->client, CNET_OWNER_COMPARE_TIMEOUT_MS);
  if (first_error == SALTS_OK && status != SALTS_OK) first_error = status;
  status = fixture->api->client_destroy(&fixture->client);
  if (first_error == SALTS_OK && status != SALTS_OK) first_error = status;
  return first_error;
}

static int compare_u64(const void *left, const void *right) {
  const uint64_t lhs = *(const uint64_t *)left;
  const uint64_t rhs = *(const uint64_t *)right;
  return lhs < rhs ? -1 : lhs > rhs;
}

static int compare_run_sample(const char *dso_path, native_io_backend_kind backend_kind,
                              compare_result *out_result) {
  compare_api api;
  compare_server server;
  compare_client client;
  unsigned char payload[CNET_OWNER_COMPARE_PAYLOAD_BYTES];
  uint64_t latencies[CNET_OWNER_COMPARE_EXCHANGES];
  uint64_t wall_started = 0u;
  bool network_started = false;
  bool server_initialized = false;
  int status;

  memset(&server, 0, sizeof(server));
  memset(&client, 0, sizeof(client));
  memset(out_result, 0, sizeof(*out_result));
  memset(payload, 0x5a, sizeof(payload));
  status = compare_api_load(&api, dso_path);
  if (status != SALTS_OK) return status;
  status = compare_network_start();
  if (status == SALTS_OK) network_started = true;
  if (status == SALTS_OK) {
    status = compare_server_init(&server);
    if (status == SALTS_OK) server_initialized = true;
  }
  if (status == SALTS_OK) status = compare_server_start(&server);
  if (status == SALTS_OK) status = compare_client_init(&client, &api, &server.address, backend_kind);
  for (size_t warmup = 0u; status == SALTS_OK && warmup < CNET_OWNER_COMPARE_WARMUPS; ++warmup)
    status = compare_exchange(&client, payload);
  if (status == SALTS_OK) wall_started = salts_hrtime();
  for (size_t exchange = 0u; status == SALTS_OK && exchange < CNET_OWNER_COMPARE_EXCHANGES;
       ++exchange) {
    const uint64_t started = salts_hrtime();
    status = compare_exchange(&client, payload);
    if (status == SALTS_OK) latencies[exchange] = salts_hrtime() - started;
  }
  if (status == SALTS_OK) {
    out_result->wall_ns = salts_hrtime() - wall_started;
    if (out_result->wall_ns == 0u) status = SALTS_ERANGE;
  }
  if (status == SALTS_OK) {
    qsort(latencies, CNET_OWNER_COMPARE_EXCHANGES, sizeof(latencies[0]), compare_u64);
    out_result->p50_ns =
        latencies[(CNET_OWNER_COMPARE_EXCHANGES - 1u) * 50u / 100u];
    out_result->p95_ns =
        latencies[(CNET_OWNER_COMPARE_EXCHANGES - 1u) * 95u / 100u];
    out_result->rate_per_second =
        (double)CNET_OWNER_COMPARE_EXCHANGES * 1000000000.0 / (double)out_result->wall_ns;
  }

  {
    const int destroy_status = compare_client_destroy(&client);
    if (status == SALTS_OK) status = destroy_status;
  }
  if (server_initialized && status != SALTS_OK) compare_server_abort(&server);
  if (server_initialized) {
    const int server_status = compare_server_finish(&server);
    if (status == SALTS_OK) status = server_status;
  }
  if (network_started) compare_network_stop();
  compare_api_unload(&api);
  return status;
}

static double compare_delta_percent(double baseline, double candidate) {
  return (candidate / baseline - 1.0) * 100.0;
}

static int compare_write_report(const char *baseline_path, const char *candidate_path,
                                const char *report_path, const compare_result *baseline,
                                const compare_result *candidate, const bool *candidate_first) {
  double baseline_p50[CNET_OWNER_COMPARE_REPEATS];
  double candidate_p50[CNET_OWNER_COMPARE_REPEATS];
  double baseline_p95[CNET_OWNER_COMPARE_REPEATS];
  double candidate_p95[CNET_OWNER_COMPARE_REPEATS];
  double baseline_rate[CNET_OWNER_COMPARE_REPEATS];
  double candidate_rate[CNET_OWNER_COMPARE_REPEATS];
  cnet_benchmark_summary p50_delta = {0};
  cnet_benchmark_summary p95_delta = {0};
  cnet_benchmark_summary rate_delta = {0};
  size_t faster = 0u;
  size_t ties = 0u;
  size_t slower = 0u;
  FILE *report;
  int status = SALTS_OK;

  for (size_t repeat = 0u; repeat < CNET_OWNER_COMPARE_REPEATS; ++repeat) {
    baseline_p50[repeat] = (double)baseline[repeat].p50_ns;
    candidate_p50[repeat] = (double)candidate[repeat].p50_ns;
    baseline_p95[repeat] = (double)baseline[repeat].p95_ns;
    candidate_p95[repeat] = (double)candidate[repeat].p95_ns;
    baseline_rate[repeat] = baseline[repeat].rate_per_second;
    candidate_rate[repeat] = candidate[repeat].rate_per_second;
    if (candidate[repeat].p50_ns < baseline[repeat].p50_ns) ++faster;
    else if (candidate[repeat].p50_ns == baseline[repeat].p50_ns) ++ties;
    else ++slower;
  }
  status = cnet_benchmark_summarize_paired_delta(baseline_p50, candidate_p50,
                                                  CNET_OWNER_COMPARE_REPEATS, &p50_delta);
  if (status == SALTS_OK)
    status = cnet_benchmark_summarize_paired_delta(baseline_p95, candidate_p95,
                                                    CNET_OWNER_COMPARE_REPEATS, &p95_delta);
  if (status == SALTS_OK)
    status = cnet_benchmark_summarize_paired_delta(baseline_rate, candidate_rate,
                                                    CNET_OWNER_COMPARE_REPEATS, &rate_delta);
  if (status != SALTS_OK) return status;

  report = fopen(report_path, "w");
  if (report == NULL) return SALTS_EIO;
  fprintf(report, "# CNet owner base-SHA versus candidate\n\n");
  fprintf(report, "- baseline DSO: `%s`\n", baseline_path);
  fprintf(report, "- candidate DSO: `%s`\n", candidate_path);
  fprintf(report, "- workload: TCP loopback, %u bytes, %u warmups, %u measured RTTs, %u paired repeats\n\n",
          (unsigned int)CNET_OWNER_COMPARE_PAYLOAD_BYTES,
          (unsigned int)CNET_OWNER_COMPARE_WARMUPS,
          (unsigned int)CNET_OWNER_COMPARE_EXCHANGES,
          (unsigned int)CNET_OWNER_COMPARE_REPEATS);
  fprintf(report,
          "| repeat | order | base p50 us | candidate p50 us | p50 delta %% | base p95 us | "
          "candidate p95 us | p95 delta %% | base RTT/s | candidate RTT/s | rate delta %% |\n");
  fprintf(report,
          "| ---: | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t repeat = 0u; repeat < CNET_OWNER_COMPARE_REPEATS; ++repeat) {
    fprintf(report,
            "| %zu | %s | %.3f | %.3f | %.3f | %.3f | %.3f | %.3f | %.1f | %.1f | %.3f |\n",
            repeat + 1u, candidate_first[repeat] ? "candidate/base" : "base/candidate",
            baseline_p50[repeat] / 1000.0, candidate_p50[repeat] / 1000.0,
            compare_delta_percent(baseline_p50[repeat], candidate_p50[repeat]),
            baseline_p95[repeat] / 1000.0, candidate_p95[repeat] / 1000.0,
            compare_delta_percent(baseline_p95[repeat], candidate_p95[repeat]),
            baseline_rate[repeat], candidate_rate[repeat],
            compare_delta_percent(baseline_rate[repeat], candidate_rate[repeat]));
  }
  fprintf(report, "\n## Paired summary\n\n");
  fprintf(report, "- p50 delta: median %.3f%%, MAD %.3f pp\n", p50_delta.median, p50_delta.mad);
  fprintf(report, "- p95 delta: median %.3f%%, MAD %.3f pp\n", p95_delta.median, p95_delta.mad);
  fprintf(report, "- rate delta: median %.3f%%, MAD %.3f pp\n", rate_delta.median, rate_delta.mad);
  fprintf(report, "- p50 direction: candidate faster=%zu, tie=%zu, slower=%zu\n", faster, ties,
          slower);
  if (fclose(report) != 0) return SALTS_EIO;
  return SALTS_OK;
}

int cnet_owner_compare_run(const char *baseline_path, const char *candidate_path,
                           const char *report_path) {
  compare_result baseline[CNET_OWNER_COMPARE_REPEATS];
  compare_result candidate[CNET_OWNER_COMPARE_REPEATS];
  bool candidate_first[CNET_OWNER_COMPARE_REPEATS];
  native_io_backend_kind backend_kind;
  int status = SALTS_OK;

  if (baseline_path == NULL || candidate_path == NULL || report_path == NULL) return SALTS_EINVAL;
  if (!compare_backend_kind(&backend_kind)) {
    fprintf(stderr, "invalid CNET_IO_BENCHMARK_BACKEND\n");
    return SALTS_EINVAL;
  }
  for (size_t repeat = 0u; status == SALTS_OK && repeat < CNET_OWNER_COMPARE_REPEATS; ++repeat) {
    candidate_first[repeat] = (repeat & 1u) != 0u;
    if (candidate_first[repeat]) {
      status = compare_run_sample(candidate_path, backend_kind, &candidate[repeat]);
      if (status == SALTS_OK)
        status = compare_run_sample(baseline_path, backend_kind, &baseline[repeat]);
    } else {
      status = compare_run_sample(baseline_path, backend_kind, &baseline[repeat]);
      if (status == SALTS_OK)
        status = compare_run_sample(candidate_path, backend_kind, &candidate[repeat]);
    }
    if (status != SALTS_OK)
      fprintf(stderr, "CNet owner comparison repeat %zu failed with status %d\n", repeat + 1u,
              status);
  }
  if (status == SALTS_OK)
    status = compare_write_report(baseline_path, candidate_path, report_path, baseline, candidate,
                                  candidate_first);
  return status;
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--self-test") == 0) {
    const double baseline[] = {100.0, 100.0, 100.0, 100.0, 100.0};
    const double candidate[] = {95.0, 96.0, 97.0, 98.0, 99.0};
    cnet_benchmark_summary summary = {0};
    if (cnet_benchmark_summarize_paired_delta(baseline, candidate, 5u, &summary) != SALTS_OK)
      return 1;
    {
      const double error = summary.median + 3.0;
      return error >= -1e-12 && error <= 1e-12 ? 0 : 1;
    }
  }
  if (argc != 4) {
    fprintf(stderr,
            "usage: %s <baseline-cnet-dso> <candidate-cnet-dso> <report-path>\n",
            argc > 0 ? argv[0] : "cnet_owner_lifecycle_compare");
    return 2;
  }
  return cnet_owner_compare_run(argv[1], argv[2], argv[3]);
}
