#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
  #define _POSIX_C_SOURCE 200809L
#endif

#include "tinytest.h"

#include <cflow/cflow.h>
#include <cflow/io_native.h>

#include <turbo/clock.h>
#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #if defined(interface)
    #undef interface
  #endif
  #include <psapi.h>
  #include <windows.h>
  #include <winsock2.h>
  #include <ws2tcpip.h>
typedef SOCKET network_socket;
  #define NETWORK_INVALID_SOCKET INVALID_SOCKET
#else
  #include <arpa/inet.h>
  #include <errno.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <signal.h>
  #include <sys/resource.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
typedef int network_socket;
  #define NETWORK_INVALID_SOCKET (-1)
#endif

enum {
  NETWORK_REQUEST_CAPACITY = 2u,
  NETWORK_SOURCE_MAX_WINDOW = 8u,
  NETWORK_TEST_SEND_BUFFER = 4096u,
  NETWORK_TEST_LARGE_PAYLOAD = 1048576u,
  NETWORK_LATENCY_SAMPLES = 200u,
  NETWORK_LATENCY_EXCHANGES = 128u,
  NETWORK_LATENCY_PAYLOAD = 64u,
  NETWORK_THROUGHPUT_SAMPLES = 50u,
  NETWORK_THROUGHPUT_EXCHANGES = 128u,
  NETWORK_TCP_THROUGHPUT_PAYLOAD = 16384u,
  NETWORK_UDP_THROUGHPUT_PAYLOAD = 8192u,
  NETWORK_MAX_SAMPLES = 10000u,
  NETWORK_MAX_EXCHANGES = 10000u,
  NETWORK_TCP_MAX_PAYLOAD = 65536u,
  NETWORK_UDP_MAX_PAYLOAD = 65507u
};

static const uint64_t NETWORK_WAIT_TIMEOUT_NS = UINT64_C(5000000000);

typedef enum network_protocol { NETWORK_PROTOCOL_TCP = 0, NETWORK_PROTOCOL_UDP } network_protocol;

typedef enum network_wait_mode { NETWORK_WAIT_BLOCKING = 0, NETWORK_WAIT_BUSY } network_wait_mode;

typedef enum network_peer_mode { NETWORK_PEER_RAW = 0, NETWORK_PEER_NATIVE } network_peer_mode;

typedef enum network_driver_mode {
  NETWORK_DRIVER_ACTOR = 0,
  NETWORK_DRIVER_SOURCE,
  NETWORK_DRIVER_DIRECT,
  NETWORK_DRIVER_VECTOR
} network_driver_mode;

typedef enum network_workload {
  NETWORK_WORKLOAD_ROUND_TRIP = 0,
  NETWORK_WORKLOAD_PIPELINE
} network_workload;

static int network_parse_wait_mode(const char *text, network_wait_mode *out);
static int network_parse_peer_mode(const char *text, network_peer_mode *out);
static int network_parse_driver_mode(const char *text, network_driver_mode *out);
static int network_parse_workload(const char *text, network_workload *out);
static int network_parse_stage_timing(const char *text, bool *out);
static int network_validate_configuration(network_driver_mode driver, network_peer_mode peer,
                                          network_wait_mode wait_mode, bool stage_timing);
static int network_validate_workload(network_workload workload,
                                     network_protocol protocol,
                                     network_driver_mode driver,
                                     network_peer_mode peer, bool throughput);
static void network_preserve_cleanup_status(int candidate, int *status);
static double network_application_mib_per_second(uint64_t application_bytes, uint64_t elapsed_ns);
static double network_application_mib_per_cpu_second(uint64_t application_bytes, uint64_t cpu_ns);
static double network_exchanges_per_second(size_t exchanges, uint64_t elapsed_ns);

typedef struct network_operation {
  cflow_io_native_operation native;
  struct sockaddr_storage address;
  size_t source_result_index;
} network_operation;

typedef struct network_operation_slot {
  network_operation operation;
  bool in_use;
} network_operation_slot;

typedef struct network_vector_operation {
  cflow_io_native_vector_operation native;
  cflow_io_native_buffer_span spans[2];
} network_vector_operation;

typedef struct network_wake_latch network_wake_latch;

typedef struct network_source_result {
  cflow_io_completion completion;
  struct sockaddr_storage address;
  size_t address_length;
  uint64_t completed_ns;
  bool ready;
} network_source_result;

typedef struct network_source_driver {
  cflow_graph surface;
  cflow_graph normalized;
  cflow_scheduler scheduler;
  cflow_source source;
  cflow_io_source_owner owner;
  cflow_run run;
  cflow_sink_callbacks sink_callbacks;
  cflow_sink sink;
  network_operation_slot operation_slots[NETWORK_SOURCE_MAX_WINDOW];
  cflow_io_native_operation pending_operations[NETWORK_SOURCE_MAX_WINDOW];
  network_source_result batch_results[NETWORK_SOURCE_MAX_WINDOW];
  size_t pending_count;
  size_t pending_index;
  size_t sink_values;
  const char *sink_error;
  network_wake_latch *wake_latch;
  network_wait_mode wait_mode;
  size_t window_capacity;
  bool surface_initialized;
  bool normalized_initialized;
  bool scheduler_initialized;
  bool owner_initialized;
  bool run_initialized;
} network_source_driver;

typedef struct network_stage_measurement {
  bool enabled;
  uint64_t operations;
  uint64_t admission_ns;
  uint64_t completion_drive_ns;
} network_stage_measurement;

typedef struct network_completion_probe {
  cflow_io_request_id ids[NETWORK_REQUEST_CAPACITY];
  cflow_io_completion values[NETWORK_REQUEST_CAPACITY];
  struct sockaddr_storage addresses[NETWORK_REQUEST_CAPACITY];
  size_t address_lengths[NETWORK_REQUEST_CAPACITY];
  size_t count;
} network_completion_probe;

struct network_wake_latch {
  turbo_mutex_t mutex;
  turbo_cond_t changed;
  bool pending;
};

typedef struct network_server {
  network_protocol protocol;
  network_socket socket_value;
  size_t exchanges;
  size_t payload_size;
  unsigned char *buffer;
  int status;
} network_server;

typedef struct network_endpoint {
  cflow_io_native_backend backend;
  cflow_executor executor;
  cflow_io_actor actor;
  network_source_driver source;
  network_operation_slot operation_slots[NETWORK_REQUEST_CAPACITY];
  network_completion_probe completions;
  network_stage_measurement *stages;
  uint64_t pending_admission_ns;
  uint64_t completion_drive_started_ns;
  network_driver_mode driver_mode;
  bool actor_initialized;
  bool executor_initialized;
  bool backend_initialized;
} network_endpoint;

typedef struct network_fixture {
  network_endpoint client;
  network_endpoint native_server;
  network_wake_latch wake_latch;
  network_wait_mode wait_mode;
  network_peer_mode peer_mode;
  network_driver_mode driver_mode;
  network_stage_measurement stages;
  network_socket client_socket;
  network_socket server_socket;
  struct sockaddr_in client_address;
  struct sockaddr_in server_address;
  turbo_thread_t server_thread;
  network_server server;
  bool server_started;
  bool wake_latch_initialized;
  bool socket_runtime_initialized;
} network_fixture;

typedef struct network_measurement {
  uint64_t *latencies;
  size_t latency_capacity;
  size_t latency_count;
  uint64_t wall_ns;
  uint64_t cpu_ns;
  uint64_t peak_rss_bytes;
} network_measurement;

static int network_exchange_source_pipeline_batch(
    network_fixture *fixture, const unsigned char *sent,
    unsigned char *received, size_t payload_size, size_t exchanges,
    uint64_t *completion_latencies);

static int network_last_error(void) {
#if defined(_WIN32)
  return -WSAGetLastError();
#else
  return -errno;
#endif
}

static bool network_socket_call_interrupted(int result) {
#if defined(_WIN32)
  (void)result;
  return false;
#else
  return result < 0 && errno == EINTR;
#endif
}

static int network_socket_runtime_init(void) {
#if defined(_WIN32)
  WSADATA data;
  const int status = WSAStartup(MAKEWORD(2, 2), &data);
  return status == 0 ? TURBO_OK : -status;
#else
  return TURBO_OK;
#endif
}

static int network_socket_runtime_destroy(void) {
#if defined(_WIN32)
  return WSACleanup() == 0 ? TURBO_OK : network_last_error();
#else
  return TURBO_OK;
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
  return ioctlsocket(socket_value, FIONBIO, &enabled) == 0 ? TURBO_OK : network_last_error();
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

static int network_set_timeout(network_socket socket_value) {
#if defined(_WIN32)
  const DWORD timeout_ms = 5000u;
  if (setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms,
                 sizeof(timeout_ms)) != 0 ||
      setsockopt(socket_value, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout_ms,
                 sizeof(timeout_ms)) != 0)
    return network_last_error();
#else
  const struct timeval timeout = {5, 0};
  if (setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
      setsockopt(socket_value, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0)
    return network_last_error();
#endif
  return TURBO_OK;
}

static int network_set_send_buffer(network_socket socket_value, int size) {
  if (size <= 0) return TURBO_EINVAL;
#if defined(_WIN32)
  return setsockopt(socket_value, SOL_SOCKET, SO_SNDBUF, (const char *)&size, sizeof(size)) == 0
             ? TURBO_OK
             : network_last_error();
#else
  return setsockopt(socket_value, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) == 0
             ? TURBO_OK
             : network_last_error();
#endif
}

static int network_bind_loopback(network_socket socket_value, struct sockaddr_in *address) {
#if defined(_WIN32)
  int address_length = (int)sizeof(*address);
#else
  socklen_t address_length = (socklen_t)sizeof(*address);
#endif
  memset(address, 0, sizeof(*address));
  address->sin_family = AF_INET;
  address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address->sin_port = 0;
  if (bind(socket_value, (const struct sockaddr *)address, (int)sizeof(*address)) != 0)
    return network_last_error();
  if (getsockname(socket_value, (struct sockaddr *)address, &address_length) != 0)
    return network_last_error();
  return TURBO_OK;
}

static int network_make_tcp_pair(network_fixture *fixture, bool client_nonblocking) {
  network_socket listener = NETWORK_INVALID_SOCKET;
  int status;
  listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == NETWORK_INVALID_SOCKET) return network_last_error();
  status = network_bind_loopback(listener, &fixture->server_address);
  if (status == TURBO_OK && listen(listener, 1) != 0) status = network_last_error();
  if (status == TURBO_OK) {
    fixture->client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fixture->client_socket == NETWORK_INVALID_SOCKET) status = network_last_error();
  }
  if (status == TURBO_OK &&
      connect(fixture->client_socket, (const struct sockaddr *)&fixture->server_address,
              (int)sizeof(fixture->server_address)) != 0)
    status = network_last_error();
  if (status == TURBO_OK) {
    fixture->server_socket = accept(listener, NULL, NULL);
    if (fixture->server_socket == NETWORK_INVALID_SOCKET) status = network_last_error();
  }
  network_close(listener);
  if (status == TURBO_OK && client_nonblocking)
    status = network_set_nonblocking(fixture->client_socket);
  return status;
}

static int network_make_udp_pair(network_fixture *fixture, bool client_nonblocking) {
  int status;
  fixture->client_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  fixture->server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fixture->client_socket == NETWORK_INVALID_SOCKET ||
      fixture->server_socket == NETWORK_INVALID_SOCKET)
    return network_last_error();
  status = network_bind_loopback(fixture->client_socket, &fixture->client_address);
  if (status == TURBO_OK)
    status = network_bind_loopback(fixture->server_socket, &fixture->server_address);
  if (status == TURBO_OK && client_nonblocking)
    status = network_set_nonblocking(fixture->client_socket);
  return status;
}

static int network_recv_exact(network_socket socket_value, unsigned char *buffer, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    int count;
    do {
      count = recv(socket_value, (char *)buffer + offset, (int)(size - offset), 0);
    } while (network_socket_call_interrupted(count));
    if (count == 0) return TURBO_EOF;
    if (count < 0) return network_last_error();
    offset += (size_t)count;
  }
  return TURBO_OK;
}

static int network_send_exact(network_socket socket_value, const unsigned char *buffer,
                              size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    int count;
    do {
      count = send(socket_value, (const char *)buffer + offset, (int)(size - offset), 0);
    } while (network_socket_call_interrupted(count));
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
      server->status =
          network_recv_exact(server->socket_value, server->buffer, server->payload_size);
      if (server->status == TURBO_OK)
        server->status =
            network_send_exact(server->socket_value, server->buffer, server->payload_size);
    } else {
      struct sockaddr_storage source;
#if defined(_WIN32)
      int source_length = (int)sizeof(source);
#else
      socklen_t source_length = (socklen_t)sizeof(source);
#endif
      int count;
      do {
#if defined(_WIN32)
        source_length = (int)sizeof(source);
#else
        source_length = (socklen_t)sizeof(source);
#endif
        count = recvfrom(server->socket_value, (char *)server->buffer,
                         (int)server->payload_size, 0, (struct sockaddr *)&source, &source_length);
      } while (network_socket_call_interrupted(count));
      if (count != (int)server->payload_size) {
        server->status = count < 0 ? network_last_error() : TURBO_EIO;
      } else {
        do {
          count =
              sendto(server->socket_value, (const char *)server->buffer,
                     (int)server->payload_size, 0, (const struct sockaddr *)&source, source_length);
        } while (network_socket_call_interrupted(count));
        if (count != (int)server->payload_size)
          server->status = count < 0 ? network_last_error() : TURBO_EIO;
      }
    }
    if (server->status != TURBO_OK) return;
  }
}

static int network_operation_prepare(network_operation *owned,
                                     cflow_io_native_operation operation) {
  if (owned == NULL) return TURBO_EINVAL;
  memset(owned, 0, sizeof(*owned));
  owned->native = operation;
  if (operation.address == NULL)
    return operation.address_capacity == 0u && operation.address_length == 0u ? TURBO_OK
                                                                              : TURBO_EINVAL;
  if (operation.address_capacity > sizeof(owned->address) ||
      operation.address_length > operation.address_capacity)
    return TURBO_EINVAL;
  if (operation.address_length != 0u)
    memcpy(&owned->address, operation.address, operation.address_length);
  owned->native.address = &owned->address;
  return TURBO_OK;
}

static network_operation_slot *network_operation_slot_try_acquire(
    network_operation_slot *slots, size_t capacity) {
  size_t index;
  if (slots == NULL) return NULL;
  for (index = 0u; index < capacity; ++index) {
    if (!slots[index].in_use) {
      slots[index].in_use = true;
      memset(&slots[index].operation, 0, sizeof(slots[index].operation));
      return &slots[index];
    }
  }
  return NULL;
}

static void network_operation_slot_release(void *user) {
  network_operation_slot *slot;
  if (user == NULL) return;
  slot = (network_operation_slot *)((unsigned char *)user -
                                    offsetof(network_operation_slot, operation));
  memset(&slot->operation, 0, sizeof(slot->operation));
  slot->in_use = false;
}

static void network_vector_operation_release(void *user) { free(user); }

static void network_completion_record(void *user, cflow_io_request_id request_id,
                                      cflow_io_lease_id lease_id, void *operation_user,
                                      const cflow_io_completion *completion) {
  network_endpoint *endpoint = (network_endpoint *)user;
  network_completion_probe *probe = endpoint != NULL ? &endpoint->completions : NULL;
  network_operation *operation =
      endpoint != NULL && endpoint->driver_mode != NETWORK_DRIVER_VECTOR
          ? (network_operation *)operation_user : NULL;
  (void)lease_id;
  if (probe == NULL) return;
  if (probe->count < NETWORK_REQUEST_CAPACITY) {
    const size_t address_length = operation != NULL ? operation->native.address_length : 0u;
    probe->ids[probe->count] = request_id;
    probe->values[probe->count] = *completion;
    probe->address_lengths[probe->count] = address_length;
    if (operation != NULL && address_length != 0u &&
        address_length <= sizeof(probe->addresses[probe->count]) &&
        operation->native.address != NULL)
      memcpy(&probe->addresses[probe->count], operation->native.address, address_length);
    ++probe->count;
  }
}

static void network_wake_signal(void *user) {
  network_wake_latch *latch = (network_wake_latch *)user;
  if (latch == NULL) return;
  turbo_mutex_lock(&latch->mutex);
  latch->pending = true;
  turbo_cond_signal(&latch->changed);
  turbo_mutex_unlock(&latch->mutex);
}

static cflow_io_source_prepare_status
network_source_prepare(void *user, cflow_io_operation *operation, const char **error) {
  static const char *const no_operation_error = "network Source has no pending operation";
  static const char *const capacity_error = "network Source operation storage is exhausted";
  static const char *const operation_error = "network Source received an invalid operation";
  network_source_driver *driver = (network_source_driver *)user;
  network_operation_slot *slot;
  size_t result_index;
  int status;

  if (driver == NULL || operation == NULL ||
      driver->pending_index >= driver->pending_count) {
    if (error != NULL) *error = no_operation_error;
    return CFLOW_IO_SOURCE_PREPARE_ERROR;
  }
  slot = network_operation_slot_try_acquire(driver->operation_slots, driver->window_capacity);
  if (slot == NULL) {
    if (error != NULL) *error = capacity_error;
    return CFLOW_IO_SOURCE_PREPARE_ERROR;
  }
  result_index = driver->pending_index;
  status = network_operation_prepare(&slot->operation,
                                     driver->pending_operations[result_index]);
  if (status != TURBO_OK) {
    network_operation_slot_release(&slot->operation);
    if (error != NULL) *error = operation_error;
    return CFLOW_IO_SOURCE_PREPARE_ERROR;
  }
  slot->operation.source_result_index = result_index;
  ++driver->pending_index;
  *operation = (cflow_io_operation){&slot->operation, network_operation_slot_release};
  return CFLOW_IO_SOURCE_PREPARE_OPERATION;
}

static cflow_read_status network_source_encode(void *user, cflow_io_request_id request_id,
                                               cflow_io_lease_id lease_id, void *operation_user,
                                               const cflow_io_completion *completion,
                                               void *out_value, const char **error) {
  network_source_driver *driver = (network_source_driver *)user;
  network_operation *operation = (network_operation *)operation_user;
  network_source_result *result;

  (void)request_id;
  (void)lease_id;
  if (driver == NULL || operation == NULL || completion == NULL || out_value == NULL ||
      operation->source_result_index >= driver->pending_count) {
    if (error != NULL) *error = "network Source received an invalid completion";
    return CFLOW_READ_ERROR;
  }
  result = &driver->batch_results[operation->source_result_index];
  if (result->ready) {
    if (error != NULL) *error = "network Source completed one batch slot twice";
    return CFLOW_READ_ERROR;
  }
  result->completion = *completion;
  result->address_length = operation->native.address_length;
  if (result->address_length > sizeof(result->address)) {
    if (error != NULL) *error = "network Source completion address exceeds storage";
    return CFLOW_READ_ERROR;
  }
  if (result->address_length != 0u && operation->native.address != NULL)
    memcpy(&result->address, operation->native.address, result->address_length);
  result->completed_ns = turbo_hrtime();
  result->ready = true;
  *(int *)out_value = 1;
  return CFLOW_READ_VALUE;
}

static void network_source_drive(void *user) {
  network_source_driver *driver = (network_source_driver *)user;
  if (driver != NULL && driver->wait_mode == NETWORK_WAIT_BLOCKING)
    network_wake_signal(driver->wake_latch);
}

static bool network_source_sink_value(void *user, const cmeta_type_desc *type, const void *value) {
  network_source_driver *driver = (network_source_driver *)user;
  if (driver == NULL) return false;
  if (type == NULL || !cmeta_type_equal(type, &cmeta_type_int) || value == NULL) {
    driver->sink_error = "network Source sink received an invalid value";
    return false;
  }
  ++driver->sink_values;
  return true;
}

static void network_source_sink_error(void *user, const char *message) {
  network_source_driver *driver = (network_source_driver *)user;
  if (driver != NULL) driver->sink_error = message;
}

static void network_source_sink_done(void *user) {
  network_source_driver *driver = (network_source_driver *)user;
  if (driver != NULL && driver->sink_error == NULL)
    driver->sink_error = "network Source completed before fixture shutdown";
}

static int network_source_pump(network_source_driver *driver) {
  size_t progressed = 0u;
  int status;
  if (driver == NULL || !driver->run_initialized) return TURBO_EINVAL;
  (void)cflow_scheduler_run_until_idle(&driver->scheduler, 0u);
  status = cflow_io_source_owner_run_ready(&driver->owner, 64u, &progressed);
  if (status != TURBO_OK) return status;
  (void)cflow_scheduler_run_until_idle(&driver->scheduler, 0u);
  if (driver->sink_error != NULL || cflow_run_error(&driver->run) != NULL) return TURBO_EIO;
  return TURBO_OK;
}

static int network_source_driver_destroy(network_source_driver *driver);

static int network_source_driver_init(network_source_driver *driver,
                                      cflow_io_native_backend *backend, network_wait_mode wait_mode,
                                      network_wake_latch *wake_latch,
                                      size_t window_capacity) {
  cflow_io_source_config config = {0};
  int status = TURBO_OK;
  if (driver == NULL || backend == NULL || window_capacity == 0u ||
      window_capacity > NETWORK_SOURCE_MAX_WINDOW)
    return TURBO_EINVAL;
  memset(driver, 0, sizeof(*driver));
  driver->normalized.root = CMETA_INVALID_ID;
  driver->wait_mode = wait_mode;
  driver->wake_latch = wake_latch;
  driver->window_capacity = window_capacity;
  cflow_graph_init(&driver->surface, &cmeta_type_int);
  driver->surface_initialized = true;
  if (!cflow_graph_normalize(&driver->normalized, &driver->surface)) {
    status = TURBO_ENOMEM;
    goto cleanup;
  }
  driver->normalized_initialized = true;
  if (!cflow_scheduler_test_init(&driver->scheduler)) {
    status = TURBO_ENOMEM;
    goto cleanup;
  }
  driver->scheduler_initialized = true;
  config.name = "network-native-operation";
  config.type = &cmeta_type_int;
  config.backend = cflow_io_native_backend_actor_ops();
  config.backend_user = backend;
  config.prepare = network_source_prepare;
  config.encode = network_source_encode;
  config.user = driver;
  config.drive = network_source_drive;
  config.drive_user = driver;
  status = cflow_source_from_io_actor_windowed(&driver->source, &driver->owner, &config,
                                               window_capacity);
  if (status != TURBO_OK) goto cleanup;
  driver->owner_initialized = true;
  driver->sink_callbacks = (cflow_sink_callbacks){
      network_source_sink_value, network_source_sink_error, network_source_sink_done, driver};
  driver->sink = cflow_sink_from_callbacks(&driver->sink_callbacks);
  if (!cflow_run_open(&driver->run, &driver->normalized, &driver->source, &driver->scheduler,
                      &driver->sink)) {
    status = TURBO_EIO;
    goto cleanup;
  }
  driver->run_initialized = true;
  return TURBO_OK;

cleanup:
  network_preserve_cleanup_status(network_source_driver_destroy(driver), &status);
  return status;
}

static int network_select_backend(cflow_io_native_backend_kind *out) {
  const char *requested = getenv("CFLOW_NETWORK_BACKEND");
  if (out == NULL) return TURBO_EINVAL;
  if (requested != NULL) {
    if (strcmp(requested, "epoll") == 0) *out = CFLOW_IO_NATIVE_EPOLL;
    else if (strcmp(requested, "kqueue") == 0) *out = CFLOW_IO_NATIVE_KQUEUE;
    else if (strcmp(requested, "iocp") == 0) *out = CFLOW_IO_NATIVE_IOCP;
    else if (strcmp(requested, "io_uring") == 0) *out = CFLOW_IO_NATIVE_IO_URING;
    else if (strcmp(requested, "poll") == 0) *out = CFLOW_IO_NATIVE_POLL;
    else return TURBO_EINVAL;
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

static int network_parse_wait_mode(const char *text, network_wait_mode *out) {
  if (out == NULL) return TURBO_EINVAL;
  if (text == NULL || strcmp(text, "blocking") == 0) {
    *out = NETWORK_WAIT_BLOCKING;
    return TURBO_OK;
  }
  if (strcmp(text, "busy") == 0) {
    *out = NETWORK_WAIT_BUSY;
    return TURBO_OK;
  }
  return TURBO_EINVAL;
}

static int network_parse_peer_mode(const char *text, network_peer_mode *out) {
  if (out == NULL) return TURBO_EINVAL;
  if (text == NULL || strcmp(text, "raw") == 0) {
    *out = NETWORK_PEER_RAW;
    return TURBO_OK;
  }
  if (strcmp(text, "native") == 0) {
    *out = NETWORK_PEER_NATIVE;
    return TURBO_OK;
  }
  return TURBO_EINVAL;
}

static int network_parse_driver_mode(const char *text, network_driver_mode *out) {
  if (out == NULL) return TURBO_EINVAL;
  if (text == NULL || strcmp(text, "actor") == 0) {
    *out = NETWORK_DRIVER_ACTOR;
    return TURBO_OK;
  }
  if (strcmp(text, "source") == 0) {
    *out = NETWORK_DRIVER_SOURCE;
    return TURBO_OK;
  }
  if (strcmp(text, "direct") == 0) {
    *out = NETWORK_DRIVER_DIRECT;
    return TURBO_OK;
  }
  if (strcmp(text, "vector") == 0) {
    *out = NETWORK_DRIVER_VECTOR;
    return TURBO_OK;
  }
  return TURBO_EINVAL;
}

static int network_parse_workload(const char *text, network_workload *out) {
  if (out == NULL) return TURBO_EINVAL;
  if (text == NULL || strcmp(text, "round-trip") == 0) {
    *out = NETWORK_WORKLOAD_ROUND_TRIP;
    return TURBO_OK;
  }
  if (strcmp(text, "pipeline") == 0) {
    *out = NETWORK_WORKLOAD_PIPELINE;
    return TURBO_OK;
  }
  return TURBO_EINVAL;
}

static int network_parse_stage_timing(const char *text, bool *out) {
  if (out == NULL) return TURBO_EINVAL;
  if (text == NULL || strcmp(text, "0") == 0) {
    *out = false;
    return TURBO_OK;
  }
  if (strcmp(text, "1") == 0) {
    *out = true;
    return TURBO_OK;
  }
  return TURBO_EINVAL;
}

static int network_validate_configuration(network_driver_mode driver, network_peer_mode peer,
                                          network_wait_mode wait_mode, bool stage_timing) {
  if (driver == NETWORK_DRIVER_SOURCE && peer != NETWORK_PEER_RAW) return TURBO_EINVAL;
  if (driver == NETWORK_DRIVER_DIRECT &&
      (peer != NETWORK_PEER_RAW || wait_mode != NETWORK_WAIT_BLOCKING || stage_timing))
    return TURBO_EINVAL;
  return TURBO_OK;
}

static int network_validate_workload(network_workload workload,
                                     network_protocol protocol,
                                     network_driver_mode driver,
                                     network_peer_mode peer, bool throughput) {
  if (workload == NETWORK_WORKLOAD_ROUND_TRIP) return TURBO_OK;
  if (workload != NETWORK_WORKLOAD_PIPELINE ||
      protocol != NETWORK_PROTOCOL_UDP || driver != NETWORK_DRIVER_SOURCE ||
      peer != NETWORK_PEER_RAW || !throughput)
    return TURBO_EINVAL;
  return TURBO_OK;
}

static int network_stage_measurement_add_operations(
    network_stage_measurement *measurement, size_t operations,
    uint64_t admission_ns, uint64_t completion_drive_ns) {
  if (measurement == NULL) return TURBO_EINVAL;
  if (!measurement->enabled) return TURBO_OK;
  if (operations == 0u || operations > UINT64_MAX - measurement->operations ||
      admission_ns > UINT64_MAX - measurement->admission_ns ||
      completion_drive_ns > UINT64_MAX - measurement->completion_drive_ns)
    return TURBO_ERANGE;
  measurement->operations += (uint64_t)operations;
  measurement->admission_ns += admission_ns;
  measurement->completion_drive_ns += completion_drive_ns;
  return TURBO_OK;
}

static int network_stage_measurement_add(network_stage_measurement *measurement,
                                         uint64_t admission_ns,
                                         uint64_t completion_drive_ns) {
  return network_stage_measurement_add_operations(
      measurement, 1u, admission_ns, completion_drive_ns);
}

static const char *network_wait_mode_name(network_wait_mode mode) {
  return mode == NETWORK_WAIT_BUSY ? "busy" : "blocking";
}

static const char *network_peer_mode_name(network_peer_mode mode) {
  return mode == NETWORK_PEER_NATIVE ? "dual-native" : "raw";
}

static const char *network_driver_mode_name(network_driver_mode mode) {
  if (mode == NETWORK_DRIVER_SOURCE) return "source";
  if (mode == NETWORK_DRIVER_DIRECT) return "direct";
  if (mode == NETWORK_DRIVER_VECTOR) return "vector";
  return "actor";
}

static const char *network_workload_name(network_workload workload) {
  return workload == NETWORK_WORKLOAD_PIPELINE ? "pipeline" : "round-trip";
}

static const char *network_backend_name(cflow_io_native_backend_kind kind) {
  switch (kind) {
  case CFLOW_IO_NATIVE_EPOLL:
    return "epoll";
  case CFLOW_IO_NATIVE_KQUEUE:
    return "kqueue";
  case CFLOW_IO_NATIVE_IOCP:
    return "iocp";
  case CFLOW_IO_NATIVE_IO_URING:
    return "io_uring";
  case CFLOW_IO_NATIVE_POLL:
    return "poll";
  }
  return "unknown";
}

static const char *network_result_backend_name(network_driver_mode driver,
                                               cflow_io_native_backend_kind kind) {
  return driver == NETWORK_DRIVER_DIRECT ? "socket" : network_backend_name(kind);
}

static int network_endpoint_init(network_endpoint *endpoint,
                                 cflow_io_native_backend_kind backend_kind,
                                 network_wait_mode wait_mode, network_wake_latch *wake_latch,
                                 network_driver_mode driver_mode,
                                 size_t source_window_capacity) {
  const size_t backend_capacity = driver_mode == NETWORK_DRIVER_SOURCE
                                      ? source_window_capacity
                                      : NETWORK_REQUEST_CAPACITY;
  cflow_io_native_backend_config backend_config = {backend_kind, backend_capacity,
                                                   backend_capacity};
  cflow_io_actor_config actor_config;
  int status;
  memset(endpoint, 0, sizeof(*endpoint));
  endpoint->driver_mode = driver_mode;
  if (source_window_capacity == 0u ||
      source_window_capacity > NETWORK_SOURCE_MAX_WINDOW)
    return TURBO_EINVAL;
  if (driver_mode == NETWORK_DRIVER_DIRECT) return TURBO_OK;
  status = cflow_io_native_backend_init(&endpoint->backend, &backend_config);
  if (status != TURBO_OK) return status;
  endpoint->backend_initialized = true;
  if (driver_mode == NETWORK_DRIVER_SOURCE)
    return network_source_driver_init(&endpoint->source, &endpoint->backend, wait_mode,
                                      wake_latch, source_window_capacity);
  if (!cflow_executor_manual_init_with_capacity(&endpoint->executor, NETWORK_REQUEST_CAPACITY))
    return TURBO_ENOMEM;
  endpoint->executor_initialized = true;
  memset(&actor_config, 0, sizeof(actor_config));
  actor_config.request_capacity = NETWORK_REQUEST_CAPACITY;
  actor_config.command_capacity = NETWORK_REQUEST_CAPACITY;
  actor_config.executor = &endpoint->executor;
  actor_config.backend = driver_mode == NETWORK_DRIVER_VECTOR
                             ? cflow_io_native_backend_vector_actor_ops()
                             : cflow_io_native_backend_actor_ops();
  actor_config.backend_user = &endpoint->backend;
  actor_config.completion = network_completion_record;
  actor_config.completion_user = endpoint;
  if (wait_mode == NETWORK_WAIT_BLOCKING && driver_mode != NETWORK_DRIVER_DIRECT) {
    actor_config.wake = network_wake_signal;
    actor_config.wake_user = wake_latch;
  }
  status = cflow_io_actor_init(&endpoint->actor, &actor_config);
  if (status != TURBO_OK) return status;
  endpoint->actor_initialized = true;
  return TURBO_OK;
}

static int network_fixture_init_with_driver_window(
    network_fixture *fixture, network_protocol protocol,
    cflow_io_native_backend_kind backend_kind, network_wait_mode wait_mode,
    network_peer_mode peer_mode, network_driver_mode driver_mode,
    size_t source_window_capacity, size_t total_exchanges, size_t payload_size) {
  int status;
  if (fixture == NULL || source_window_capacity == 0u ||
      source_window_capacity > NETWORK_SOURCE_MAX_WINDOW || total_exchanges == 0u ||
      payload_size == 0u)
    return TURBO_EINVAL;
  status = network_validate_configuration(driver_mode, peer_mode, wait_mode, false);
  if (status != TURBO_OK) return status;
  if (driver_mode == NETWORK_DRIVER_VECTOR && protocol != NETWORK_PROTOCOL_TCP)
    return TURBO_ENOTSUP;
  if (driver_mode == NETWORK_DRIVER_VECTOR &&
      (!cflow_io_native_backend_vector_operation_supported(
           backend_kind, CFLOW_IO_NATIVE_TCP_RECV_VECTOR) ||
       !cflow_io_native_backend_vector_operation_supported(
           backend_kind, CFLOW_IO_NATIVE_TCP_SEND_VECTOR)))
    return TURBO_ENOTSUP;
  memset(fixture, 0, sizeof(*fixture));
  fixture->wait_mode = wait_mode;
  fixture->peer_mode = peer_mode;
  fixture->driver_mode = driver_mode;
  fixture->client_socket = NETWORK_INVALID_SOCKET;
  fixture->server_socket = NETWORK_INVALID_SOCKET;
  if (driver_mode == NETWORK_DRIVER_DIRECT) {
    status = network_socket_runtime_init();
    if (status != TURBO_OK) return status;
    fixture->socket_runtime_initialized = true;
  }
  if (wait_mode == NETWORK_WAIT_BLOCKING && driver_mode != NETWORK_DRIVER_DIRECT) {
    turbo_mutex_init(&fixture->wake_latch.mutex);
    if (fixture->wake_latch.mutex == NULL) return TURBO_ENOMEM;
    turbo_cond_init(&fixture->wake_latch.changed);
    if (fixture->wake_latch.changed == NULL) {
      turbo_mutex_destroy(&fixture->wake_latch.mutex);
      return TURBO_ENOMEM;
    }
    fixture->wake_latch_initialized = true;
  }
  status = network_endpoint_init(&fixture->client, backend_kind, wait_mode, &fixture->wake_latch,
                                 driver_mode, source_window_capacity);
  if (status != TURBO_OK) return status;
  fixture->client.stages = &fixture->stages;
  if (peer_mode == NETWORK_PEER_NATIVE) {
    status = network_endpoint_init(&fixture->native_server, backend_kind, wait_mode,
                                   &fixture->wake_latch, NETWORK_DRIVER_ACTOR, 1u);
    if (status != TURBO_OK) return status;
    fixture->native_server.stages = &fixture->stages;
  }
  status = protocol == NETWORK_PROTOCOL_TCP
               ? network_make_tcp_pair(fixture, driver_mode != NETWORK_DRIVER_DIRECT)
               : network_make_udp_pair(fixture, driver_mode != NETWORK_DRIVER_DIRECT);
  if (status == TURBO_OK && peer_mode == NETWORK_PEER_NATIVE)
    status = network_set_nonblocking(fixture->server_socket);
  if (status == TURBO_OK && peer_mode == NETWORK_PEER_RAW)
    status = network_set_timeout(fixture->server_socket);
  if (status == TURBO_OK && driver_mode == NETWORK_DRIVER_DIRECT)
    status = network_set_timeout(fixture->client_socket);
  if (status != TURBO_OK) return status;
  fixture->server.protocol = protocol;
  fixture->server.socket_value = fixture->server_socket;
  fixture->server.exchanges = total_exchanges;
  fixture->server.payload_size = payload_size;
  fixture->server.buffer = (unsigned char *)malloc(payload_size);
  if (fixture->server.buffer == NULL) return TURBO_ENOMEM;
  if (peer_mode == NETWORK_PEER_RAW) {
    status = turbo_thread_create(&fixture->server_thread, network_server_entry, &fixture->server);
    if (status != TURBO_OK) return status;
    fixture->server_started = true;
  }
  return TURBO_OK;
}

static int network_fixture_init_with_driver(network_fixture *fixture, network_protocol protocol,
                                            cflow_io_native_backend_kind backend_kind,
                                            network_wait_mode wait_mode,
                                            network_peer_mode peer_mode,
                                            network_driver_mode driver_mode, size_t total_exchanges,
                                            size_t payload_size) {
  return network_fixture_init_with_driver_window(
      fixture, protocol, backend_kind, wait_mode, peer_mode, driver_mode, 1u,
      total_exchanges, payload_size);
}

static int network_fixture_init(network_fixture *fixture, network_protocol protocol,
                                cflow_io_native_backend_kind backend_kind,
                                network_wait_mode wait_mode, network_peer_mode peer_mode,
                                size_t total_exchanges, size_t payload_size) {
  return network_fixture_init_with_driver(fixture, protocol, backend_kind, wait_mode, peer_mode,
                                          NETWORK_DRIVER_ACTOR, total_exchanges, payload_size);
}

static bool network_endpoint_get_actor_stats(const network_endpoint *endpoint,
                                             cflow_io_actor_stats *out) {
  if (endpoint == NULL || out == NULL) return false;
  if (endpoint->driver_mode == NETWORK_DRIVER_SOURCE) {
    cflow_io_source_stats source_stats = {0};
    if (!cflow_io_source_owner_get_stats(&endpoint->source.owner, &source_stats)) return false;
    *out = source_stats.actor;
    return true;
  }
  return cflow_io_actor_get_stats(&endpoint->actor, out);
}

static void network_pump(network_fixture *fixture) {
  (void)cflow_io_actor_run_ready(&fixture->client.actor, 64u);
  (void)cflow_executor_run_ready(&fixture->client.executor);
  if (fixture->peer_mode == NETWORK_PEER_NATIVE) {
    (void)cflow_io_actor_run_ready(&fixture->native_server.actor, 64u);
    (void)cflow_executor_run_ready(&fixture->native_server.executor);
  }
}

static int network_wait_busy(network_fixture *fixture, network_endpoint *endpoint,
                             size_t completion_count) {
  uint64_t started = turbo_hrtime();
  while (endpoint->completions.count < completion_count) {
    network_pump(fixture);
    if (turbo_hrtime() - started >= NETWORK_WAIT_TIMEOUT_NS) return TURBO_ETIMEDOUT;
    turbo_thread_yield();
  }
  return TURBO_OK;
}

static int network_wait_blocking(network_fixture *fixture, network_endpoint *endpoint,
                                 size_t completion_count) {
  const uint64_t started = turbo_hrtime();
  while (endpoint->completions.count < completion_count) {
    uint64_t elapsed;
    uint64_t remaining;
    network_pump(fixture);
    if (endpoint->completions.count >= completion_count) break;
    elapsed = turbo_hrtime() - started;
    if (elapsed >= NETWORK_WAIT_TIMEOUT_NS) return TURBO_ETIMEDOUT;
    remaining = NETWORK_WAIT_TIMEOUT_NS - elapsed;
    turbo_mutex_lock(&fixture->wake_latch.mutex);
    if (!fixture->wake_latch.pending)
      (void)turbo_cond_timedwait(&fixture->wake_latch.changed, &fixture->wake_latch.mutex,
                                 remaining);
    fixture->wake_latch.pending = false;
    turbo_mutex_unlock(&fixture->wake_latch.mutex);
  }
  return TURBO_OK;
}

static int network_wait(network_fixture *fixture, network_endpoint *endpoint,
                        size_t completion_count) {
  return fixture->wait_mode == NETWORK_WAIT_BUSY
             ? network_wait_busy(fixture, endpoint, completion_count)
             : network_wait_blocking(fixture, endpoint, completion_count);
}

static int network_submit_native_operation(network_endpoint *endpoint,
                                           cflow_io_native_operation operation,
                                           cflow_io_lease_id lease) {
  const bool timing_enabled =
      endpoint != NULL && endpoint->stages != NULL && endpoint->stages->enabled;
  const uint64_t admission_started = timing_enabled ? turbo_hrtime() : 0u;
  network_operation_slot *slot;
  cflow_io_operation actor_operation;
  cflow_io_submit_result submitted;
  int status;
  if (endpoint == NULL) return TURBO_EINVAL;
  slot = network_operation_slot_try_acquire(endpoint->operation_slots, NETWORK_REQUEST_CAPACITY);
  if (slot == NULL) return TURBO_EBUSY;
  status = network_operation_prepare(&slot->operation, operation);
  if (status != TURBO_OK) {
    network_operation_slot_release(&slot->operation);
    return status;
  }
  actor_operation = (cflow_io_operation){&slot->operation, network_operation_slot_release};
  endpoint->completions.count = 0u;
  submitted = cflow_io_actor_try_submit(&endpoint->actor, lease, &actor_operation);
  if (submitted.status != CFLOW_IO_SUBMIT_ACCEPTED) {
    network_operation_slot_release(&slot->operation);
    return TURBO_EBUSY;
  }
  if (timing_enabled) {
    const uint64_t admitted = turbo_hrtime();
    endpoint->pending_admission_ns = admitted - admission_started;
    endpoint->completion_drive_started_ns = admitted;
  }
  return TURBO_OK;
}

static int network_submit_vector_operation(network_endpoint *endpoint,
                                           cflow_io_native_operation operation,
                                           cflow_io_lease_id lease) {
  const bool timing_enabled =
      endpoint != NULL && endpoint->stages != NULL && endpoint->stages->enabled;
  const uint64_t admission_started = timing_enabled ? turbo_hrtime() : 0u;
  network_vector_operation *owned;
  cflow_io_operation actor_operation;
  cflow_io_submit_result submitted;
  size_t first_length;

  if (endpoint == NULL ||
      (operation.kind != CFLOW_IO_NATIVE_TCP_RECV &&
       operation.kind != CFLOW_IO_NATIVE_TCP_SEND) ||
      operation.buffer == NULL || operation.length == 0u)
    return TURBO_EINVAL;
  owned = (network_vector_operation *)malloc(sizeof(*owned));
  if (owned == NULL) return TURBO_ENOMEM;
  memset(owned, 0, sizeof(*owned));
  first_length = operation.length == 1u ? 1u : operation.length / 2u;
  owned->spans[0] = (cflow_io_native_buffer_span){operation.buffer, first_length};
  if (first_length < operation.length) {
    owned->spans[1] = (cflow_io_native_buffer_span){
        (unsigned char *)operation.buffer + first_length,
        operation.length - first_length};
  }
  owned->native = (cflow_io_native_vector_operation){
      operation.kind == CFLOW_IO_NATIVE_TCP_RECV
          ? CFLOW_IO_NATIVE_TCP_RECV_VECTOR : CFLOW_IO_NATIVE_TCP_SEND_VECTOR,
      operation.socket, owned->spans, first_length < operation.length ? 2u : 1u};
  actor_operation = (cflow_io_operation){owned, network_vector_operation_release};
  endpoint->completions.count = 0u;
  submitted = cflow_io_actor_try_submit(&endpoint->actor, lease, &actor_operation);
  if (submitted.status != CFLOW_IO_SUBMIT_ACCEPTED) {
    free(owned);
    return TURBO_EBUSY;
  }
  if (timing_enabled) {
    const uint64_t admitted = turbo_hrtime();
    endpoint->pending_admission_ns = admitted - admission_started;
    endpoint->completion_drive_started_ns = admitted;
  }
  return TURBO_OK;
}

static int network_submit_tcp_operation(network_endpoint *endpoint,
                                        cflow_io_native_operation operation,
                                        cflow_io_lease_id lease) {
  return endpoint->driver_mode == NETWORK_DRIVER_VECTOR
             ? network_submit_vector_operation(endpoint, operation, lease)
             : network_submit_native_operation(endpoint, operation, lease);
}

static int network_finish_native_operation_details(network_fixture *fixture,
                                                   network_endpoint *endpoint, size_t *bytes,
                                                   void *address, size_t address_capacity,
                                                   size_t *address_length) {
  int status = network_wait(fixture, endpoint, 1u);
  if (status == TURBO_OK) {
    if (endpoint->completions.values[0].kind != CFLOW_IO_COMPLETION_OK)
      status = endpoint->completions.values[0].kind == CFLOW_IO_COMPLETION_FAILED
                   ? endpoint->completions.values[0].error
                   : TURBO_EIO;
    else {
      *bytes = endpoint->completions.values[0].bytes;
      if (address_length != NULL) {
        *address_length = endpoint->completions.address_lengths[0];
        if (address == NULL || *address_length == 0u || *address_length > address_capacity)
          status = TURBO_EIO;
        else memcpy(address, &endpoint->completions.addresses[0], *address_length);
      }
    }
  }
  if (endpoint->completions.count != 0u &&
      cflow_io_actor_acknowledge(&endpoint->actor, endpoint->completions.ids[0]) !=
          CFLOW_IO_ACK_RELEASED)
    status = TURBO_EIO;
  endpoint->completions.count = 0u;
  if (endpoint->completion_drive_started_ns != 0u) {
    const uint64_t completion_drive_ns = turbo_hrtime() - endpoint->completion_drive_started_ns;
    if (status == TURBO_OK)
      status = network_stage_measurement_add(endpoint->stages, endpoint->pending_admission_ns,
                                             completion_drive_ns);
    endpoint->pending_admission_ns = 0u;
    endpoint->completion_drive_started_ns = 0u;
  }
  return status;
}

static int network_finish_native_operation(network_fixture *fixture, network_endpoint *endpoint,
                                           size_t *bytes) {
  return network_finish_native_operation_details(fixture, endpoint, bytes, NULL, 0u, NULL);
}

static int network_run_source_operations(
    network_fixture *fixture, network_endpoint *endpoint,
    const cflow_io_native_operation *operations, size_t operation_count,
    network_source_result *results) {
  network_source_driver *driver;
  bool timing_enabled;
  uint64_t admission_started;
  uint64_t admission_ns = 0u;
  uint64_t completion_drive_started = 0u;
  const uint64_t started = turbo_hrtime();
  size_t target_values;
  int status = TURBO_OK;
  if (fixture == NULL || endpoint == NULL || operations == NULL || results == NULL)
    return TURBO_EINVAL;
  driver = &endpoint->source;
  timing_enabled = endpoint->stages != NULL && endpoint->stages->enabled;
  admission_started = timing_enabled ? turbo_hrtime() : 0u;
  if (operation_count == 0u || operation_count > driver->window_capacity ||
      operation_count > NETWORK_SOURCE_MAX_WINDOW || !driver->run_initialized ||
      driver->pending_count != 0u || operation_count > SIZE_MAX - driver->sink_values)
    return TURBO_EINVAL;
  target_values = driver->sink_values + operation_count;
  memcpy(driver->pending_operations, operations,
         operation_count * sizeof(*operations));
  memset(driver->batch_results, 0,
         operation_count * sizeof(*driver->batch_results));
  driver->pending_count = operation_count;
  driver->pending_index = 0u;
  if (!cflow_run_request(&driver->run, operation_count)) {
    driver->pending_count = 0u;
    return TURBO_EIO;
  }
  if (timing_enabled) {
    const uint64_t admitted = turbo_hrtime();
    admission_ns = admitted - admission_started;
    completion_drive_started = admitted;
  }
  while (driver->sink_values < target_values) {
    uint64_t elapsed;
    status = network_source_pump(driver);
    if (status != TURBO_OK) break;
    if (driver->sink_values >= target_values) break;
    elapsed = turbo_hrtime() - started;
    if (elapsed >= NETWORK_WAIT_TIMEOUT_NS) {
      status = TURBO_ETIMEDOUT;
      break;
    }
    if (fixture->wait_mode == NETWORK_WAIT_BLOCKING) {
      turbo_mutex_lock(&fixture->wake_latch.mutex);
      if (!fixture->wake_latch.pending)
        (void)turbo_cond_timedwait(&fixture->wake_latch.changed, &fixture->wake_latch.mutex,
                                   NETWORK_WAIT_TIMEOUT_NS - elapsed);
      fixture->wake_latch.pending = false;
      turbo_mutex_unlock(&fixture->wake_latch.mutex);
    } else {
      turbo_thread_yield();
    }
  }
  if (status == TURBO_OK && driver->pending_index != operation_count)
    status = TURBO_EIO;
  for (size_t index = 0u; status == TURBO_OK && index < operation_count;
       ++index) {
    if (!driver->batch_results[index].ready)
      status = TURBO_EIO;
    else if (driver->batch_results[index].completion.kind ==
             CFLOW_IO_COMPLETION_FAILED)
      status = driver->batch_results[index].completion.error;
    else if (driver->batch_results[index].completion.kind !=
             CFLOW_IO_COMPLETION_OK)
      status = TURBO_EIO;
  }
  if (status == TURBO_OK)
    memcpy(results, driver->batch_results,
           operation_count * sizeof(*results));
  if (status == TURBO_OK && timing_enabled)
    status = network_stage_measurement_add_operations(
        endpoint->stages, operation_count, admission_ns,
        turbo_hrtime() - completion_drive_started);
  if (status == TURBO_OK) {
    driver->pending_count = 0u;
    driver->pending_index = 0u;
  }
  return status;
}

static int network_run_source_operation(network_fixture *fixture,
                                        network_endpoint *endpoint,
                                        cflow_io_native_operation operation,
                                        size_t *bytes) {
  network_source_result result = {0};
  int status;
  if (bytes == NULL) return TURBO_EINVAL;
  status = network_run_source_operations(fixture, endpoint, &operation, 1u,
                                         &result);
  if (status == TURBO_OK) *bytes = result.completion.bytes;
  return status;
}

static bool network_ipv4_endpoint_equal(const void *actual_address, size_t actual_length,
                                        const struct sockaddr_in *expected) {
  const struct sockaddr_in *actual = (const struct sockaddr_in *)actual_address;
  return actual != NULL && expected != NULL && actual_length >= sizeof(*actual) &&
         actual->sin_family == AF_INET && expected->sin_family == AF_INET &&
         actual->sin_addr.s_addr == expected->sin_addr.s_addr &&
         actual->sin_port == expected->sin_port;
}

static int network_exchange_source_pipeline_batch(
    network_fixture *fixture, const unsigned char *sent,
    unsigned char *received, size_t payload_size, size_t exchanges,
    uint64_t *completion_latencies) {
  cflow_io_native_operation operations[NETWORK_SOURCE_MAX_WINDOW] = {0};
  network_source_result results[NETWORK_SOURCE_MAX_WINDOW] = {0};
  struct sockaddr_storage source_addresses[NETWORK_SOURCE_MAX_WINDOW];
  uint64_t started;
  int status;

  if (fixture == NULL || sent == NULL || received == NULL ||
      completion_latencies == NULL || payload_size == 0u || exchanges == 0u ||
      exchanges > fixture->client.source.window_capacity ||
      exchanges > NETWORK_SOURCE_MAX_WINDOW ||
      exchanges > SIZE_MAX / payload_size ||
      fixture->driver_mode != NETWORK_DRIVER_SOURCE ||
      fixture->peer_mode != NETWORK_PEER_RAW)
    return TURBO_EINVAL;

  memset(received, 0, exchanges * payload_size);
  memset(source_addresses, 0, sizeof(source_addresses));
  started = turbo_hrtime();
  for (size_t exchange = 0u; exchange < exchanges; ++exchange) {
    operations[exchange] = (cflow_io_native_operation){
        CFLOW_IO_NATIVE_UDP_SEND_TO,
        (uintptr_t)fixture->client_socket,
        (void *)sent,
        payload_size,
        &fixture->server_address,
        sizeof(fixture->server_address),
        sizeof(fixture->server_address)};
  }
  status = network_run_source_operations(
      fixture, &fixture->client, operations, exchanges, results);
  for (size_t exchange = 0u; status == TURBO_OK && exchange < exchanges;
       ++exchange) {
    if (results[exchange].completion.bytes != payload_size)
      status = TURBO_EIO;
  }
  if (status != TURBO_OK) return status;

  memset(operations, 0, sizeof(operations));
  memset(results, 0, sizeof(results));
  for (size_t exchange = 0u; exchange < exchanges; ++exchange) {
    operations[exchange] = (cflow_io_native_operation){
        CFLOW_IO_NATIVE_UDP_RECV_FROM,
        (uintptr_t)fixture->client_socket,
        received + exchange * payload_size,
        payload_size,
        &source_addresses[exchange],
        sizeof(source_addresses[exchange]),
        0u};
  }
  status = network_run_source_operations(
      fixture, &fixture->client, operations, exchanges, results);
  for (size_t exchange = 0u; status == TURBO_OK && exchange < exchanges;
       ++exchange) {
    if (results[exchange].completion.bytes != payload_size ||
        results[exchange].completed_ns < started ||
        !network_ipv4_endpoint_equal(&results[exchange].address,
                                     results[exchange].address_length,
                                     &fixture->server_address) ||
        memcmp(received + exchange * payload_size, sent, payload_size) != 0)
      status = TURBO_EIO;
    else
      completion_latencies[exchange] =
          results[exchange].completed_ns - started;
  }
  return status;
}

static int network_exchange_direct(network_fixture *fixture, network_protocol protocol,
                                   const unsigned char *sent, unsigned char *received,
                                   size_t payload_size) {
  if (protocol == NETWORK_PROTOCOL_TCP) {
    int status = network_send_exact(fixture->client_socket, sent, payload_size);
    if (status == TURBO_OK)
      status = network_recv_exact(fixture->client_socket, received, payload_size);
    if (status != TURBO_OK) return status;
  } else {
    struct sockaddr_storage source;
#if defined(_WIN32)
    int source_length = (int)sizeof(source);
#else
    socklen_t source_length = (socklen_t)sizeof(source);
#endif
    int count;
    do {
      count = sendto(fixture->client_socket, (const char *)sent, (int)payload_size, 0,
                     (const struct sockaddr *)&fixture->server_address,
                     (int)sizeof(fixture->server_address));
    } while (network_socket_call_interrupted(count));
    if (count != (int)payload_size) return count < 0 ? network_last_error() : TURBO_EIO;
    memset(&source, 0, sizeof(source));
    do {
#if defined(_WIN32)
      source_length = (int)sizeof(source);
#else
      source_length = (socklen_t)sizeof(source);
#endif
      count = recvfrom(fixture->client_socket, (char *)received, (int)payload_size, 0,
                       (struct sockaddr *)&source, &source_length);
    } while (network_socket_call_interrupted(count));
    if (count != (int)payload_size) return count < 0 ? network_last_error() : TURBO_EIO;
    if (!network_ipv4_endpoint_equal(&source, (size_t)source_length, &fixture->server_address))
      return TURBO_EIO;
  }
  return memcmp(sent, received, payload_size) == 0 ? TURBO_OK : TURBO_EIO;
}

static int network_run_native_operation(network_fixture *fixture, network_endpoint *endpoint,
                                        cflow_io_native_operation operation,
                                        cflow_io_lease_id lease, size_t *bytes) {
  if (endpoint->driver_mode == NETWORK_DRIVER_SOURCE) {
    (void)lease;
    return network_run_source_operation(fixture, endpoint, operation, bytes);
  }
  int status = network_submit_native_operation(endpoint, operation, lease);
  return status == TURBO_OK ? network_finish_native_operation(fixture, endpoint, bytes) : status;
}

static int network_run_tcp_operation(network_fixture *fixture,
                                     network_endpoint *endpoint,
                                     cflow_io_native_operation operation,
                                     cflow_io_lease_id lease,
                                     size_t *bytes) {
  int status;
  if (endpoint->driver_mode == NETWORK_DRIVER_SOURCE)
    return network_run_source_operation(fixture, endpoint, operation, bytes);
  status = network_submit_tcp_operation(endpoint, operation, lease);
  return status == TURBO_OK
             ? network_finish_native_operation(fixture, endpoint, bytes)
             : status;
}

static int network_transfer_tcp_exact(network_fixture *fixture, network_endpoint *sender,
                                      network_socket sender_socket, unsigned char *sender_buffer,
                                      network_endpoint *receiver, network_socket receiver_socket,
                                      unsigned char *receiver_buffer, size_t length,
                                      uint64_t *next_lease) {
  size_t sent = 0u;
  size_t received = 0u;
  while (received < length) {
    size_t sent_now = 0u;
    size_t received_now = 0u;
    cflow_io_native_operation receive_operation = {CFLOW_IO_NATIVE_TCP_RECV,
                                                   (uintptr_t)receiver_socket,
                                                   receiver_buffer + received,
                                                   length - received,
                                                   NULL,
                                                   0u,
                                                   0u};
    int receive_status;
    if (sent < length) {
      cflow_io_native_operation send_operation = {CFLOW_IO_NATIVE_TCP_SEND,
                                                  (uintptr_t)sender_socket,
                                                  sender_buffer + sent,
                                                  length - sent,
                                                  NULL,
                                                  0u,
                                                  0u};
      int send_status;
      receive_status =
          network_submit_tcp_operation(receiver, receive_operation, (*next_lease)++);
      if (receive_status != TURBO_OK) return receive_status;
      send_status = network_submit_tcp_operation(sender, send_operation, (*next_lease)++);
      if (send_status != TURBO_OK) return send_status;
      send_status = network_finish_native_operation(fixture, sender, &sent_now);
      receive_status = network_finish_native_operation(fixture, receiver, &received_now);
      if (send_status != TURBO_OK) return send_status;
      if (receive_status != TURBO_OK) return receive_status;
    } else {
      receive_status =
          network_submit_tcp_operation(receiver, receive_operation, (*next_lease)++);
      if (receive_status == TURBO_OK)
        receive_status = network_finish_native_operation(fixture, receiver, &received_now);
      if (receive_status != TURBO_OK) return receive_status;
    }
    if ((sent < length && (sent_now == 0u || sent_now > length - sent)) || received_now == 0u ||
        received_now > length - received)
      return TURBO_EIO;
    sent += sent_now;
    received += received_now;
  }
  return sent == received ? TURBO_OK : TURBO_EIO;
}

static int network_transfer_udp_datagram(network_fixture *fixture, network_endpoint *sender,
                                         network_socket sender_socket, unsigned char *sender_buffer,
                                         const void *destination_address, size_t destination_length,
                                         network_endpoint *receiver, network_socket receiver_socket,
                                         unsigned char *receiver_buffer, void *source_address,
                                         size_t source_capacity, size_t *source_length,
                                         const struct sockaddr_in *expected_source,
                                         size_t payload_size, uint64_t *next_lease) {
  size_t sent = 0u;
  size_t received = 0u;
  cflow_io_native_operation receive_operation = {CFLOW_IO_NATIVE_UDP_RECV_FROM,
                                                 (uintptr_t)receiver_socket,
                                                 receiver_buffer,
                                                 payload_size,
                                                 source_address,
                                                 source_capacity,
                                                 0u};
  cflow_io_native_operation send_operation = {
      CFLOW_IO_NATIVE_UDP_SEND_TO, (uintptr_t)sender_socket, sender_buffer,     payload_size,
      (void *)destination_address, destination_length,       destination_length};
  int receive_status;
  int send_status;
  if (source_length == NULL || next_lease == NULL || destination_address == NULL ||
      destination_length == 0u || source_address == NULL || source_capacity == 0u ||
      expected_source == NULL)
    return TURBO_EINVAL;
  *source_length = 0u;
  memset(source_address, 0, source_capacity);
  receive_status = network_submit_native_operation(receiver, receive_operation, (*next_lease)++);
  if (receive_status != TURBO_OK) return receive_status;
  send_status = network_submit_native_operation(sender, send_operation, (*next_lease)++);
  if (send_status != TURBO_OK) return send_status;
  send_status = network_finish_native_operation(fixture, sender, &sent);
  receive_status = network_finish_native_operation_details(
      fixture, receiver, &received, source_address, source_capacity, source_length);
  if (send_status != TURBO_OK) return send_status;
  if (receive_status != TURBO_OK) return receive_status;
  if (sent != payload_size || received != payload_size || *source_length == 0u ||
      *source_length > source_capacity ||
      !network_ipv4_endpoint_equal(source_address, *source_length, expected_source))
    return TURBO_EIO;
  return TURBO_OK;
}

static int network_exchange(network_fixture *fixture, network_protocol protocol,
                            unsigned char *sent, unsigned char *received, size_t payload_size,
                            uint64_t lease_base) {
  struct sockaddr_storage source_address;
  size_t send_offset = 0u;
  size_t receive_offset = 0u;
  size_t bytes = 0u;
  int status = TURBO_OK;
  memset(received, 0, payload_size);
  memset(&source_address, 0, sizeof(source_address));
  if (fixture->driver_mode == NETWORK_DRIVER_DIRECT)
    return network_exchange_direct(fixture, protocol, sent, received, payload_size);
  if (fixture->peer_mode == NETWORK_PEER_NATIVE) {
    uint64_t next_lease = lease_base;
    memset(fixture->server.buffer, 0, payload_size);
    if (protocol == NETWORK_PROTOCOL_TCP) {
      status = network_transfer_tcp_exact(fixture, &fixture->client, fixture->client_socket, sent,
                                          &fixture->native_server, fixture->server_socket,
                                          fixture->server.buffer, payload_size, &next_lease);
      if (status == TURBO_OK)
        status = network_transfer_tcp_exact(
            fixture, &fixture->native_server, fixture->server_socket, fixture->server.buffer,
            &fixture->client, fixture->client_socket, received, payload_size, &next_lease);
    } else {
      struct sockaddr_storage response_address;
      size_t source_length = 0u;
      size_t response_length = 0u;
      memset(&response_address, 0, sizeof(response_address));
      status = network_transfer_udp_datagram(
          fixture, &fixture->client, fixture->client_socket, sent, &fixture->server_address,
          sizeof(fixture->server_address), &fixture->native_server, fixture->server_socket,
          fixture->server.buffer, &source_address, sizeof(source_address), &source_length,
          &fixture->client_address, payload_size, &next_lease);
      if (status == TURBO_OK)
        status = network_transfer_udp_datagram(
            fixture, &fixture->native_server, fixture->server_socket, fixture->server.buffer,
            &source_address, source_length, &fixture->client, fixture->client_socket, received,
            &response_address, sizeof(response_address), &response_length, &fixture->server_address,
            payload_size, &next_lease);
    }
    if (status == TURBO_OK && memcmp(sent, received, payload_size) != 0) status = TURBO_EIO;
    return status;
  }
  while (protocol == NETWORK_PROTOCOL_TCP && send_offset < payload_size) {
    cflow_io_native_operation operation = {CFLOW_IO_NATIVE_TCP_SEND,
                                           (uintptr_t)fixture->client_socket,
                                           sent + send_offset,
                                           payload_size - send_offset,
                                           NULL,
                                           0u,
                                           0u};
    status = network_run_tcp_operation(fixture, &fixture->client, operation,
                                       lease_base++, &bytes);
    if (status != TURBO_OK) return status;
    if (bytes == 0u) return TURBO_EIO;
    send_offset += bytes;
  }
  if (protocol == NETWORK_PROTOCOL_UDP) {
    cflow_io_native_operation operation = {CFLOW_IO_NATIVE_UDP_SEND_TO,
                                           (uintptr_t)fixture->client_socket,
                                           sent,
                                           payload_size,
                                           &fixture->server_address,
                                           sizeof(fixture->server_address),
                                           sizeof(fixture->server_address)};
    status =
        network_run_native_operation(fixture, &fixture->client, operation, lease_base++, &bytes);
    if (status != TURBO_OK || bytes != payload_size) return TURBO_EIO;
  }
  while (protocol == NETWORK_PROTOCOL_TCP && receive_offset < payload_size) {
    cflow_io_native_operation operation = {CFLOW_IO_NATIVE_TCP_RECV,
                                           (uintptr_t)fixture->client_socket,
                                           received + receive_offset,
                                           payload_size - receive_offset,
                                           NULL,
                                           0u,
                                           0u};
    status = network_run_tcp_operation(fixture, &fixture->client, operation,
                                       lease_base++, &bytes);
    if (status != TURBO_OK) return status;
    if (bytes == 0u) return TURBO_EIO;
    receive_offset += bytes;
  }
  if (protocol == NETWORK_PROTOCOL_UDP) {
    cflow_io_native_operation operation = {CFLOW_IO_NATIVE_UDP_RECV_FROM,
                                           (uintptr_t)fixture->client_socket,
                                           received,
                                           payload_size,
                                           &source_address,
                                           sizeof(source_address),
                                           0u};
    status = network_run_native_operation(fixture, &fixture->client, operation, lease_base, &bytes);
    if (status != TURBO_OK || bytes != payload_size) return TURBO_EIO;
  }
  if (memcmp(sent, received, payload_size) != 0) status = TURBO_EIO;
  return status;
}

static void network_preserve_cleanup_status(int candidate, int *status) {
  if (*status == TURBO_OK && candidate != TURBO_OK) *status = candidate;
}

static int network_acknowledge_completions(network_endpoint *endpoint) {
  int status = TURBO_OK;
  for (size_t index = 0u; index < endpoint->completions.count; ++index) {
    if (cflow_io_actor_acknowledge(&endpoint->actor, endpoint->completions.ids[index]) !=
        CFLOW_IO_ACK_RELEASED)
      status = TURBO_EIO;
  }
  endpoint->completions.count = 0u;
  return status;
}

static int network_forget_closed_socket(network_endpoint *endpoint,
                                        network_socket closed_socket) {
  const uint64_t started = turbo_hrtime();
  int status;
  do {
    status = cflow_io_native_backend_forget_socket(&endpoint->backend,
                                                   (uintptr_t)closed_socket);
    if (status != TURBO_EBUSY) return status;
    turbo_thread_yield();
  } while (turbo_hrtime() - started < NETWORK_WAIT_TIMEOUT_NS);
  return TURBO_ETIMEDOUT;
}

static int network_source_driver_destroy(network_source_driver *driver) {
  int status = TURBO_OK;
  if (driver == NULL) return TURBO_EINVAL;
  if (driver->run_initialized) {
    cflow_run_close(&driver->run);
    driver->run_initialized = false;
  } else if (cflow_source_valid(&driver->source)) {
    cflow_source_destroy(&driver->source);
  }
  if (driver->owner_initialized) {
    const uint64_t started = turbo_hrtime();
    while (!cflow_io_source_owner_is_quiescent(&driver->owner)) {
      size_t progressed = 0u;
      const int drive_status = cflow_io_source_owner_run_ready(&driver->owner, 64u, &progressed);
      if (drive_status != TURBO_OK && drive_status != TURBO_EBUSY)
        network_preserve_cleanup_status(drive_status, &status);
      if (driver->scheduler_initialized)
        (void)cflow_scheduler_run_until_idle(&driver->scheduler, 0u);
      if (turbo_hrtime() - started >= NETWORK_WAIT_TIMEOUT_NS)
        return status == TURBO_OK ? TURBO_ETIMEDOUT : status;
      turbo_thread_yield();
    }
    network_preserve_cleanup_status(cflow_io_source_owner_close(&driver->owner), &status);
    if (status != TURBO_OK) return status;
    driver->owner_initialized = false;
  }
  if (driver->scheduler_initialized) {
    cflow_scheduler_destroy(&driver->scheduler);
    driver->scheduler_initialized = false;
  }
  if (driver->normalized_initialized) {
    cflow_graph_destroy(&driver->normalized);
    driver->normalized_initialized = false;
  }
  if (driver->surface_initialized) {
    cflow_graph_destroy(&driver->surface);
    driver->surface_initialized = false;
  }
  return status;
}

static int network_endpoint_destroy(network_endpoint *endpoint, network_socket closed_socket) {
  int status = TURBO_OK;
  if (endpoint->source.owner_initialized || endpoint->source.run_initialized ||
      endpoint->source.scheduler_initialized || endpoint->source.normalized_initialized ||
      endpoint->source.surface_initialized) {
    status = network_source_driver_destroy(&endpoint->source);
    if (status != TURBO_OK) return status;
  }
  if (endpoint->actor_initialized) {
    const uint64_t started = turbo_hrtime();
    status = cflow_io_actor_close(&endpoint->actor);
    while (!cflow_io_actor_is_quiescent(&endpoint->actor)) {
      (void)cflow_io_actor_run_ready(&endpoint->actor, 64u);
      (void)cflow_executor_run_ready(&endpoint->executor);
      network_preserve_cleanup_status(network_acknowledge_completions(endpoint), &status);
      if (turbo_hrtime() - started >= NETWORK_WAIT_TIMEOUT_NS)
        return status == TURBO_OK ? TURBO_ETIMEDOUT : status;
      turbo_thread_yield();
    }
    network_preserve_cleanup_status(network_acknowledge_completions(endpoint), &status);
    if (status != TURBO_OK) return status;
    status = cflow_io_actor_destroy(&endpoint->actor);
    if (status != TURBO_OK) return status;
    endpoint->actor_initialized = false;
  }
  if (endpoint->backend_initialized) {
    if (closed_socket != NETWORK_INVALID_SOCKET) {
      const int forget_status = network_forget_closed_socket(endpoint, closed_socket);
      if (forget_status != TURBO_OK && forget_status != TURBO_ENOENT) return forget_status;
    }
    status = cflow_io_native_backend_shutdown(&endpoint->backend);
    if (status != TURBO_OK) return status;
    status = cflow_io_native_backend_destroy(&endpoint->backend);
    if (status != TURBO_OK) return status;
    endpoint->backend_initialized = false;
  }
  if (endpoint->executor_initialized) {
    if (!cflow_executor_shutdown(&endpoint->executor)) return TURBO_EBUSY;
    cflow_executor_destroy(&endpoint->executor);
    endpoint->executor_initialized = false;
  }
  return TURBO_OK;
}

static bool network_endpoint_destroyed(const network_endpoint *endpoint) {
  return !endpoint->actor_initialized && !endpoint->backend_initialized &&
         !endpoint->executor_initialized && !endpoint->source.owner_initialized &&
         !endpoint->source.run_initialized && !endpoint->source.scheduler_initialized &&
         !endpoint->source.normalized_initialized && !endpoint->source.surface_initialized;
}

static int network_fixture_destroy(network_fixture *fixture) {
  network_socket client_socket;
  network_socket server_socket;
  int status = TURBO_OK;
  if (fixture == NULL) return TURBO_EINVAL;
  if (fixture->server_started) {
    network_preserve_cleanup_status(turbo_thread_join(&fixture->server_thread), &status);
    turbo_thread_destroy(&fixture->server_thread);
    fixture->server_started = false;
  }
  client_socket = fixture->client_socket;
  server_socket = fixture->server_socket;
  network_close(client_socket);
  network_close(server_socket);
  fixture->client_socket = NETWORK_INVALID_SOCKET;
  fixture->server_socket = NETWORK_INVALID_SOCKET;
  network_preserve_cleanup_status(network_endpoint_destroy(&fixture->native_server, server_socket),
                                  &status);
  network_preserve_cleanup_status(network_endpoint_destroy(&fixture->client, client_socket),
                                  &status);
  if (fixture->wake_latch_initialized && network_endpoint_destroyed(&fixture->native_server) &&
      network_endpoint_destroyed(&fixture->client)) {
    turbo_cond_destroy(&fixture->wake_latch.changed);
    turbo_mutex_destroy(&fixture->wake_latch.mutex);
    fixture->wake_latch_initialized = false;
  }
  if (network_endpoint_destroyed(&fixture->native_server) &&
      network_endpoint_destroyed(&fixture->client)) {
    free(fixture->server.buffer);
    fixture->server.buffer = NULL;
  }
  if (fixture->socket_runtime_initialized) {
    const int runtime_status = network_socket_runtime_destroy();
    network_preserve_cleanup_status(runtime_status, &status);
    if (runtime_status == TURBO_OK) fixture->socket_runtime_initialized = false;
  }
  return status;
}

static size_t network_env_size(const char *name, size_t fallback, size_t maximum) {
  const char *text = getenv(name);
  char *end = NULL;
  unsigned long long value;
  if (text == NULL || *text == '\0') return fallback;
  value = strtoull(text, &end, 10);
  if (end == text || *end != '\0' || value == 0u || value > maximum) return 0u;
  return (size_t)value;
}

static uint64_t network_process_cpu_ns(void) {
#if defined(_WIN32)
  FILETIME created, exited, kernel, user;
  ULARGE_INTEGER kernel_ticks, user_ticks;
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0u;
  kernel_ticks.LowPart = kernel.dwLowDateTime;
  kernel_ticks.HighPart = kernel.dwHighDateTime;
  user_ticks.LowPart = user.dwLowDateTime;
  user_ticks.HighPart = user.dwHighDateTime;
  return (kernel_ticks.QuadPart + user_ticks.QuadPart) * UINT64_C(100);
#else
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0u;
  return ((uint64_t)usage.ru_utime.tv_sec + (uint64_t)usage.ru_stime.tv_sec) *
             UINT64_C(1000000000) +
         ((uint64_t)usage.ru_utime.tv_usec + (uint64_t)usage.ru_stime.tv_usec) * UINT64_C(1000);
#endif
}

static uint64_t network_peak_rss_bytes(void) {
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters;
  memset(&counters, 0, sizeof(counters));
  counters.cb = sizeof(counters);
  return GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))
             ? (uint64_t)counters.PeakWorkingSetSize
             : 0u;
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

static double network_application_mib_per_second(uint64_t application_bytes, uint64_t elapsed_ns) {
  const double bytes_per_mib = 1048576.0;
  const double ns_per_second = 1000000000.0;
  if (elapsed_ns == 0u) return 0.0;
  return ((double)application_bytes / bytes_per_mib) / ((double)elapsed_ns / ns_per_second);
}

static double network_application_mib_per_cpu_second(uint64_t application_bytes, uint64_t cpu_ns) {
  return network_application_mib_per_second(application_bytes, cpu_ns);
}

static double network_exchanges_per_second(size_t exchanges, uint64_t elapsed_ns) {
  const double ns_per_second = 1000000000.0;
  if (elapsed_ns == 0u) return 0.0;
  return (double)exchanges / ((double)elapsed_ns / ns_per_second);
}

static double network_stage_mean_ns(uint64_t total_ns, uint64_t operations) {
  return operations == 0u ? 0.0 : (double)total_ns / (double)operations;
}

static int network_u64_compare(const void *left, const void *right) {
  const uint64_t a = *(const uint64_t *)left;
  const uint64_t b = *(const uint64_t *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

#if !defined(_WIN32)
static volatile sig_atomic_t network_interrupt_writer = -1;

static void network_interrupt_receive(int signal_number) {
  const unsigned char value = 0x5au;
  const int writer = (int)network_interrupt_writer;
  (void)signal_number;
  if (writer >= 0) (void)write(writer, &value, sizeof(value));
}
#endif

static uint64_t network_percentile(uint64_t *sorted, size_t count, size_t numerator) {
  size_t rank = (numerator * count + 99u) / 100u;
  if (rank == 0u) rank = 1u;
  return sorted[rank - 1u];
}

spec("CFlow network benchmark configuration") {
#if !defined(_WIN32)
  it("continues a blocking socket receive after a POSIX signal interruption") {
    int sockets[2] = {-1, -1};
    struct sigaction action;
    struct sigaction previous_action;
    struct itimerval timer;
    unsigned char received = 0u;
    int status = TURBO_EIO;
    bool action_installed = false;

    memset(&action, 0, sizeof(action));
    memset(&previous_action, 0, sizeof(previous_action));
    memset(&timer, 0, sizeof(timer));
    check_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    if (sockets[0] >= 0 && sockets[1] >= 0) {
      action.sa_handler = network_interrupt_receive;
      check_equal(sigemptyset(&action.sa_mask), 0);
      if (sigaction(SIGALRM, &action, &previous_action) == 0) {
        action_installed = true;
        network_interrupt_writer = (sig_atomic_t)sockets[1];
        timer.it_value.tv_usec = 1000;
        const int timer_status = setitimer(ITIMER_REAL, &timer, NULL);
        check_equal(timer_status, 0);
        if (timer_status == 0) status = network_recv_exact(sockets[0], &received, sizeof(received));
      }
    }

    memset(&timer, 0, sizeof(timer));
    (void)setitimer(ITIMER_REAL, &timer, NULL);
    network_interrupt_writer = -1;
    if (action_installed) (void)sigaction(SIGALRM, &previous_action, NULL);
    network_close(sockets[0]);
    network_close(sockets[1]);
    check_equal(status, TURBO_OK);
    check_equal(received, 0x5au);
  }
#endif

  it("owns native UDP address storage independently from the caller") {
    struct sockaddr_in caller_address;
    network_operation owned = {0};
    cflow_io_native_operation operation;
    const struct sockaddr_in *owned_address;

    memset(&caller_address, 0, sizeof(caller_address));
    caller_address.sin_family = AF_INET;
    caller_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    caller_address.sin_port = htons(4242u);
    operation = (cflow_io_native_operation){CFLOW_IO_NATIVE_UDP_SEND_TO,
                                            (uintptr_t)NETWORK_INVALID_SOCKET,
                                            NULL,
                                            0u,
                                            &caller_address,
                                            sizeof(caller_address),
                                            sizeof(caller_address)};

    check_equal(network_operation_prepare(&owned, operation), TURBO_OK);
    check_true(owned.native.address != &caller_address);
    caller_address.sin_port = htons(4343u);
    owned_address = (const struct sockaddr_in *)owned.native.address;
    check_equal(owned_address->sin_family, AF_INET);
    check_equal(owned_address->sin_addr.s_addr, htonl(INADDR_LOOPBACK));
    check_equal(owned_address->sin_port, htons(4242u));
  }

  it("returns bounded native operation storage for reuse after release") {
    network_operation_slot slots[1] = {0};
    network_operation_slot *first =
        network_operation_slot_try_acquire(slots, 1u);

    check_not_null(first);
    check_null(network_operation_slot_try_acquire(slots, 1u));
    network_operation_slot_release(&first->operation);
    first = network_operation_slot_try_acquire(slots, 1u);
    check_true(first == &slots[0]);
    network_operation_slot_release(&first->operation);
  }

  it("compares UDP source endpoints by family address and port") {
    struct sockaddr_storage actual_storage;
    struct sockaddr_in expected;
    struct sockaddr_in *actual = (struct sockaddr_in *)&actual_storage;

    memset(&actual_storage, 0, sizeof(actual_storage));
    memset(&expected, 0, sizeof(expected));
    actual->sin_family = AF_INET;
    actual->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    actual->sin_port = htons(4242u);
    expected = *actual;
    check_true(network_ipv4_endpoint_equal(&actual_storage, sizeof(*actual), &expected));
    actual->sin_port = htons(4343u);
    check_false(network_ipv4_endpoint_equal(&actual_storage, sizeof(*actual), &expected));
  }

  it("round trips one TCP payload through the direct socket driver") {
    cflow_io_native_backend_kind backend_kind = CFLOW_IO_NATIVE_EPOLL;
    network_fixture fixture = {0};
    unsigned char sent[1024];
    unsigned char received[1024];
    int status = TURBO_OK;
    bool init_attempted = false;

    for (size_t index = 0u; index < sizeof(sent); ++index)
      sent[index] = (unsigned char)(index * 7u + 5u);
    memset(received, 0, sizeof(received));
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) {
      init_attempted = true;
      status = network_fixture_init_with_driver(&fixture, NETWORK_PROTOCOL_TCP, backend_kind,
                                                NETWORK_WAIT_BLOCKING, NETWORK_PEER_RAW,
                                                NETWORK_DRIVER_DIRECT, 1u, sizeof(sent));
    }
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) {
      check_false(fixture.client.backend_initialized);
      check_false(fixture.client.executor_initialized);
      check_false(fixture.client.actor_initialized);
      check_false(fixture.wake_latch_initialized);
      status = network_exchange(&fixture, NETWORK_PROTOCOL_TCP, sent, received, sizeof(sent),
                                UINT64_C(1));
    }
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) check_equal(memcmp(received, sent, sizeof(sent)), 0);
    if (init_attempted) check_equal(network_fixture_destroy(&fixture), TURBO_OK);
  }

  it("round trips one UDP payload through the direct socket driver") {
    cflow_io_native_backend_kind backend_kind = CFLOW_IO_NATIVE_EPOLL;
    network_fixture fixture = {0};
    unsigned char sent[1024];
    unsigned char received[1024];
    int status = TURBO_OK;
    bool init_attempted = false;

    memset(sent, 0xa7, sizeof(sent));
    memset(received, 0, sizeof(received));
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) {
      init_attempted = true;
      status = network_fixture_init_with_driver(&fixture, NETWORK_PROTOCOL_UDP, backend_kind,
                                                NETWORK_WAIT_BLOCKING, NETWORK_PEER_RAW,
                                                NETWORK_DRIVER_DIRECT, 1u, sizeof(sent));
    }
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) {
      check_false(fixture.client.backend_initialized);
      check_false(fixture.client.executor_initialized);
      check_false(fixture.client.actor_initialized);
      check_false(fixture.wake_latch_initialized);
      status = network_exchange(&fixture, NETWORK_PROTOCOL_UDP, sent, received, sizeof(sent),
                                UINT64_C(1));
    }
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) check_equal(memcmp(received, sent, sizeof(sent)), 0);
    if (init_attempted) check_equal(network_fixture_destroy(&fixture), TURBO_OK);
  }

  it("round trips one TCP payload through the requested reactive Source window") {
    cflow_io_native_backend_kind backend_kind = CFLOW_IO_NATIVE_EPOLL;
    network_fixture fixture = {0};
    unsigned char sent[1024];
    unsigned char received[1024];
    cflow_io_actor_stats actor_stats = {0};
    cflow_io_source_window_stats window_stats = {0};
    int status = network_select_backend(&backend_kind);
    bool init_attempted = false;

    for (size_t index = 0u; index < sizeof(sent); ++index)
      sent[index] = (unsigned char)(index * 13u + 11u);
    memset(received, 0, sizeof(received));
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) {
      init_attempted = true;
      status = network_fixture_init_with_driver_window(
          &fixture, NETWORK_PROTOCOL_TCP, backend_kind, NETWORK_WAIT_BLOCKING,
          NETWORK_PEER_RAW, NETWORK_DRIVER_SOURCE, 4u, 1u, sizeof(sent));
    }
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) {
      fixture.stages.enabled = true;
      status = network_exchange(&fixture, NETWORK_PROTOCOL_TCP, sent, received, sizeof(sent),
                                UINT64_C(1));
    }
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) {
      check_equal(memcmp(received, sent, sizeof(sent)), 0);
      check_equal(fixture.stages.operations, (uint64_t)2u);
      check_true(fixture.stages.completion_drive_ns > 0u);
      check_true(network_endpoint_get_actor_stats(&fixture.client, &actor_stats));
      check_equal(actor_stats.acknowledged, (uint64_t)2u);
      check_equal(actor_stats.active_requests, (size_t)0u);
      check_true(cflow_io_source_owner_get_window_stats(
          &fixture.client.source.owner, &window_stats));
      check_equal(window_stats.capacity, (size_t)4u);
      check_equal(window_stats.peak_occupied, (size_t)1u);
    }
    if (init_attempted) check_equal(network_fixture_destroy(&fixture), TURBO_OK);
  }

  it("pipelines four independent UDP exchanges through one Source window") {
    enum { PIPELINE_EXCHANGES = 4, PIPELINE_PAYLOAD_SIZE = 128 };
    cflow_io_native_backend_kind backend_kind = CFLOW_IO_NATIVE_EPOLL;
    network_fixture fixture = {0};
    unsigned char sent[PIPELINE_PAYLOAD_SIZE];
    unsigned char received[PIPELINE_EXCHANGES][PIPELINE_PAYLOAD_SIZE];
    uint64_t completion_latencies[PIPELINE_EXCHANGES] = {0};
    cflow_io_actor_stats actor_stats = {0};
    cflow_io_source_window_stats window_stats = {0};
    int status = network_select_backend(&backend_kind);
    bool init_attempted = false;

    for (size_t index = 0u; index < sizeof(sent); ++index)
      sent[index] = (unsigned char)(index * 17u + 3u);
    memset(received, 0, sizeof(received));
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) {
      init_attempted = true;
      status = network_fixture_init_with_driver_window(
          &fixture, NETWORK_PROTOCOL_UDP, backend_kind,
          NETWORK_WAIT_BLOCKING, NETWORK_PEER_RAW, NETWORK_DRIVER_SOURCE,
          PIPELINE_EXCHANGES, PIPELINE_EXCHANGES, sizeof(sent));
    }
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) {
      fixture.stages.enabled = true;
      status = network_exchange_source_pipeline_batch(
          &fixture, sent, &received[0][0], sizeof(sent),
          PIPELINE_EXCHANGES, completion_latencies);
    }
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) {
      for (size_t exchange = 0u; exchange < PIPELINE_EXCHANGES;
           ++exchange) {
        check_equal(memcmp(received[exchange], sent, sizeof(sent)), 0);
        check_true(completion_latencies[exchange] > 0u);
      }
      check_equal(fixture.stages.operations,
                  (uint64_t)(PIPELINE_EXCHANGES * 2u));
      check_true(network_endpoint_get_actor_stats(&fixture.client,
                                                  &actor_stats));
      check_equal(actor_stats.acknowledged,
                  (uint64_t)(PIPELINE_EXCHANGES * 2u));
      check_true(cflow_io_source_owner_get_window_stats(
          &fixture.client.source.owner, &window_stats));
      check_equal(window_stats.capacity, (size_t)PIPELINE_EXCHANGES);
      check_equal(window_stats.peak_occupied,
                  (size_t)PIPELINE_EXCHANGES);
    }
    if (init_attempted)
      check_equal(network_fixture_destroy(&fixture), TURBO_OK);
  }

  it("round trips one UDP payload through the reactive Source driver") {
    cflow_io_native_backend_kind backend_kind = CFLOW_IO_NATIVE_EPOLL;
    network_fixture fixture = {0};
    unsigned char sent[1024];
    unsigned char received[1024];
    int status = network_select_backend(&backend_kind);
    bool init_attempted = false;

    memset(sent, 0xa5, sizeof(sent));
    memset(received, 0, sizeof(received));
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) {
      init_attempted = true;
      status = network_fixture_init_with_driver(&fixture, NETWORK_PROTOCOL_UDP, backend_kind,
                                                NETWORK_WAIT_BLOCKING, NETWORK_PEER_RAW,
                                                NETWORK_DRIVER_SOURCE, 1u, sizeof(sent));
    }
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK)
      status = network_exchange(&fixture, NETWORK_PROTOCOL_UDP, sent, received, sizeof(sent),
                                UINT64_C(1));
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) check_equal(memcmp(received, sent, sizeof(sent)), 0);
    if (init_attempted) check_equal(network_fixture_destroy(&fixture), TURBO_OK);
  }

  it("round trips one TCP payload through dual native endpoints") {
    cflow_io_native_backend_kind backend_kind = CFLOW_IO_NATIVE_EPOLL;
    network_fixture fixture = {0};
    unsigned char sent[1024];
    unsigned char received[1024];
    int status = network_select_backend(&backend_kind);
    bool init_attempted = false;

    for (size_t index = 0u; index < sizeof(sent); ++index)
      sent[index] = (unsigned char)(index * 17u + 3u);
    memset(received, 0, sizeof(received));
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) {
      init_attempted = true;
      status = network_fixture_init(&fixture, NETWORK_PROTOCOL_TCP, backend_kind,
                                    NETWORK_WAIT_BLOCKING, NETWORK_PEER_NATIVE, 1u, sizeof(sent));
    }
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK)
      status = network_exchange(&fixture, NETWORK_PROTOCOL_TCP, sent, received, sizeof(sent),
                                UINT64_C(1));
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) check_equal(memcmp(received, sent, sizeof(sent)), 0);
    if (init_attempted) check_equal(network_fixture_destroy(&fixture), TURBO_OK);
  }

  it("round trips one TCP payload through the vectored Actor driver") {
    cflow_io_native_backend_kind backend_kind = CFLOW_IO_NATIVE_EPOLL;
    network_fixture fixture = {0};
    unsigned char sent[1024];
    unsigned char received[1024];
    int status = network_select_backend(&backend_kind);
    bool init_attempted = false;

    for (size_t index = 0u; index < sizeof(sent); ++index)
      sent[index] = (unsigned char)(index * 19u + 7u);
    memset(received, 0, sizeof(received));
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) {
      init_attempted = true;
      status = network_fixture_init_with_driver(
          &fixture, NETWORK_PROTOCOL_TCP, backend_kind, NETWORK_WAIT_BLOCKING,
          NETWORK_PEER_RAW, NETWORK_DRIVER_VECTOR, 1u, sizeof(sent));
    }
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK)
      status = network_exchange(&fixture, NETWORK_PROTOCOL_TCP, sent, received,
                                sizeof(sent), UINT64_C(1));
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) check_equal(memcmp(received, sent, sizeof(sent)), 0);
    if (init_attempted) check_equal(network_fixture_destroy(&fixture), TURBO_OK);
  }

  it("round trips one UDP datagram through dual native endpoints") {
    cflow_io_native_backend_kind backend_kind = CFLOW_IO_NATIVE_EPOLL;
    network_fixture fixture = {0};
    unsigned char sent[1024];
    unsigned char received[1024];
    int status = network_select_backend(&backend_kind);
    bool init_attempted = false;

    memset(sent, 0x5a, sizeof(sent));
    memset(received, 0, sizeof(received));
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) {
      init_attempted = true;
      status = network_fixture_init(&fixture, NETWORK_PROTOCOL_UDP, backend_kind,
                                    NETWORK_WAIT_BLOCKING, NETWORK_PEER_NATIVE, 1u, sizeof(sent));
    }
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK)
      status = network_exchange(&fixture, NETWORK_PROTOCOL_UDP, sent, received, sizeof(sent),
                                UINT64_C(1));
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) check_equal(memcmp(received, sent, sizeof(sent)), 0);
    if (init_attempted) check_equal(network_fixture_destroy(&fixture), TURBO_OK);
  }

  it("round trips a payload larger than both native send windows") {
    cflow_io_native_backend_kind backend_kind = CFLOW_IO_NATIVE_EPOLL;
    network_fixture fixture = {0};
    unsigned char *sent = (unsigned char *)malloc(NETWORK_TEST_LARGE_PAYLOAD);
    unsigned char *received = (unsigned char *)malloc(NETWORK_TEST_LARGE_PAYLOAD);
    int status = network_select_backend(&backend_kind);
    int cleanup_status = TURBO_OK;
    bool init_attempted = false;

    check_not_null(sent);
    check_not_null(received);
    if (sent != NULL && received != NULL) {
      for (size_t index = 0u; index < NETWORK_TEST_LARGE_PAYLOAD; ++index)
        sent[index] = (unsigned char)(index * 29u + 7u);
      memset(received, 0, NETWORK_TEST_LARGE_PAYLOAD);
    } else {
      status = TURBO_ENOMEM;
    }
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) {
      init_attempted = true;
      status =
          network_fixture_init(&fixture, NETWORK_PROTOCOL_TCP, backend_kind, NETWORK_WAIT_BLOCKING,
                               NETWORK_PEER_NATIVE, 1u, NETWORK_TEST_LARGE_PAYLOAD);
    }
    if (status == TURBO_OK)
      status = network_set_send_buffer(fixture.client_socket, NETWORK_TEST_SEND_BUFFER);
    if (status == TURBO_OK)
      status = network_set_send_buffer(fixture.server_socket, NETWORK_TEST_SEND_BUFFER);
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK)
      status = network_exchange(&fixture, NETWORK_PROTOCOL_TCP, sent, received,
                                NETWORK_TEST_LARGE_PAYLOAD, UINT64_C(1));
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) check_equal(memcmp(received, sent, NETWORK_TEST_LARGE_PAYLOAD), 0);
    if (init_attempted) cleanup_status = network_fixture_destroy(&fixture);
    check_equal(cleanup_status, TURBO_OK);
    if (cleanup_status == TURBO_OK) {
      free(received);
      free(sent);
    }
  }

  it("drains and acknowledges a pending native receive during cleanup") {
    cflow_io_native_backend_kind backend_kind = CFLOW_IO_NATIVE_EPOLL;
    network_fixture fixture = {0};
    unsigned char *received = (unsigned char *)calloc(1u, 1u);
    cflow_io_actor_stats actor_stats = {0};
    cflow_io_native_backend_stats native_stats = {0};
    int status = network_select_backend(&backend_kind);
    int cleanup_status = TURBO_OK;
    bool init_attempted = false;

    check_not_null(received);
    if (received == NULL) status = TURBO_ENOMEM;
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) {
      init_attempted = true;
      status = network_fixture_init(&fixture, NETWORK_PROTOCOL_TCP, backend_kind,
                                    NETWORK_WAIT_BLOCKING, NETWORK_PEER_NATIVE, 1u, 1u);
    }
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) {
      cflow_io_native_operation operation = {
          CFLOW_IO_NATIVE_TCP_RECV, (uintptr_t)fixture.client_socket, received, 1u, NULL, 0u, 0u};
      status = network_submit_native_operation(&fixture.client, operation, UINT64_C(1));
    }
    check_equal(status, TURBO_OK);
    if (status == TURBO_OK) {
      (void)cflow_io_actor_run_ready(&fixture.client.actor, 64u);
      check_true(cflow_io_actor_get_stats(&fixture.client.actor, &actor_stats));
      check_true(cflow_io_native_backend_get_stats(&fixture.client.backend, &native_stats));
      check_equal(actor_stats.backend_pending, 1u);
      check_equal(native_stats.active_requests, 1u);
    }
    if (init_attempted) cleanup_status = network_fixture_destroy(&fixture);
    check_equal(cleanup_status, TURBO_OK);
    if (cleanup_status == TURBO_OK) free(received);
  }

  it("parses raw and native peer modes and rejects unknown input") {
    network_peer_mode mode = NETWORK_PEER_NATIVE;

    check_equal(network_parse_peer_mode(NULL, &mode), TURBO_OK);
    check_equal(mode, NETWORK_PEER_RAW);
    check_equal(network_parse_peer_mode("raw", &mode), TURBO_OK);
    check_equal(mode, NETWORK_PEER_RAW);
    check_equal(network_parse_peer_mode("native", &mode), TURBO_OK);
    check_equal(mode, NETWORK_PEER_NATIVE);
    check_equal(network_parse_peer_mode("thread", &mode), TURBO_EINVAL);
    check_equal(network_parse_peer_mode("raw", NULL), TURBO_EINVAL);
  }

  it("parses direct, actor, and source drivers and rejects unsupported combinations") {
    network_driver_mode driver = NETWORK_DRIVER_SOURCE;

    check_equal(network_parse_driver_mode(NULL, &driver), TURBO_OK);
    check_equal(driver, NETWORK_DRIVER_ACTOR);
    check_equal(network_parse_driver_mode("actor", &driver), TURBO_OK);
    check_equal(driver, NETWORK_DRIVER_ACTOR);
    check_equal(network_parse_driver_mode("source", &driver), TURBO_OK);
    check_equal(driver, NETWORK_DRIVER_SOURCE);
    check_equal(network_parse_driver_mode("direct", &driver), TURBO_OK);
    check_equal(driver, NETWORK_DRIVER_DIRECT);
    check_equal(network_parse_driver_mode("vector", &driver), TURBO_OK);
    check_equal(driver, NETWORK_DRIVER_VECTOR);
    check_equal(network_parse_driver_mode("socket", &driver), TURBO_EINVAL);
    check_equal(network_parse_driver_mode("actor", NULL), TURBO_EINVAL);
    check_equal(network_validate_configuration(NETWORK_DRIVER_ACTOR, NETWORK_PEER_RAW,
                                               NETWORK_WAIT_BLOCKING, false),
                TURBO_OK);
    check_equal(network_validate_configuration(NETWORK_DRIVER_ACTOR, NETWORK_PEER_NATIVE,
                                               NETWORK_WAIT_BUSY, true),
                TURBO_OK);
    check_equal(network_validate_configuration(NETWORK_DRIVER_SOURCE, NETWORK_PEER_RAW,
                                               NETWORK_WAIT_BLOCKING, true),
                TURBO_OK);
    check_equal(network_validate_configuration(NETWORK_DRIVER_SOURCE, NETWORK_PEER_NATIVE,
                                               NETWORK_WAIT_BLOCKING, false),
                TURBO_EINVAL);
    check_equal(network_validate_configuration(NETWORK_DRIVER_DIRECT, NETWORK_PEER_RAW,
                                               NETWORK_WAIT_BLOCKING, false),
                TURBO_OK);
    check_equal(network_validate_configuration(NETWORK_DRIVER_DIRECT, NETWORK_PEER_NATIVE,
                                               NETWORK_WAIT_BLOCKING, false),
                TURBO_EINVAL);
    check_equal(network_validate_configuration(NETWORK_DRIVER_DIRECT, NETWORK_PEER_RAW,
                                               NETWORK_WAIT_BUSY, false),
                TURBO_EINVAL);
    check_equal(network_validate_configuration(NETWORK_DRIVER_DIRECT, NETWORK_PEER_RAW,
                                               NETWORK_WAIT_BLOCKING, true),
                TURBO_EINVAL);
  }

  it("parses the opt-in stage timing flag") {
    bool enabled = true;

    check_equal(network_parse_stage_timing(NULL, &enabled), TURBO_OK);
    check_false(enabled);
    check_equal(network_parse_stage_timing("0", &enabled), TURBO_OK);
    check_false(enabled);
    check_equal(network_parse_stage_timing("1", &enabled), TURBO_OK);
    check_true(enabled);
    check_equal(network_parse_stage_timing("true", &enabled), TURBO_EINVAL);
    check_equal(network_parse_stage_timing("1", NULL), TURBO_EINVAL);
  }

  it("accepts pipeline workload only for Source UDP throughput") {
    network_workload workload = NETWORK_WORKLOAD_PIPELINE;

    check_equal(network_parse_workload(NULL, &workload), TURBO_OK);
    check_equal(workload, NETWORK_WORKLOAD_ROUND_TRIP);
    check_equal(network_parse_workload("round-trip", &workload), TURBO_OK);
    check_equal(workload, NETWORK_WORKLOAD_ROUND_TRIP);
    check_equal(network_parse_workload("pipeline", &workload), TURBO_OK);
    check_equal(workload, NETWORK_WORKLOAD_PIPELINE);
    check_equal(network_parse_workload("batch", &workload), TURBO_EINVAL);
    check_equal(network_parse_workload("pipeline", NULL), TURBO_EINVAL);

    check_equal(network_validate_workload(
                    NETWORK_WORKLOAD_PIPELINE, NETWORK_PROTOCOL_UDP,
                    NETWORK_DRIVER_SOURCE, NETWORK_PEER_RAW, true),
                TURBO_OK);
    check_equal(network_validate_workload(
                    NETWORK_WORKLOAD_PIPELINE, NETWORK_PROTOCOL_TCP,
                    NETWORK_DRIVER_SOURCE, NETWORK_PEER_RAW, true),
                TURBO_EINVAL);
    check_equal(network_validate_workload(
                    NETWORK_WORKLOAD_PIPELINE, NETWORK_PROTOCOL_UDP,
                    NETWORK_DRIVER_ACTOR, NETWORK_PEER_RAW, true),
                TURBO_EINVAL);
    check_equal(network_validate_workload(
                    NETWORK_WORKLOAD_PIPELINE, NETWORK_PROTOCOL_UDP,
                    NETWORK_DRIVER_SOURCE, NETWORK_PEER_NATIVE, true),
                TURBO_EINVAL);
    check_equal(network_validate_workload(
                    NETWORK_WORKLOAD_PIPELINE, NETWORK_PROTOCOL_UDP,
                    NETWORK_DRIVER_SOURCE, NETWORK_PEER_RAW, false),
                TURBO_EINVAL);
    check_equal(network_validate_workload(
                    NETWORK_WORKLOAD_ROUND_TRIP, NETWORK_PROTOCOL_TCP,
                    NETWORK_DRIVER_ACTOR, NETWORK_PEER_RAW, false),
                TURBO_OK);
  }

  it("accumulates optional stage timing including zero-duration samples") {
    network_stage_measurement stages = {0};

    check_equal(network_stage_measurement_add(&stages, 10u, 20u), TURBO_OK);
    check_equal(stages.operations, (uint64_t)0u);
    stages.enabled = true;
    check_equal(network_stage_measurement_add(&stages, 0u, 0u), TURBO_OK);
    check_equal(stages.operations, (uint64_t)1u);
    check_equal(stages.admission_ns, (uint64_t)0u);
    check_equal(stages.completion_drive_ns, (uint64_t)0u);
    check_equal(network_stage_measurement_add(&stages, 10u, 20u), TURBO_OK);
    check_equal(stages.operations, (uint64_t)2u);
    check_equal(stages.admission_ns, (uint64_t)10u);
    check_equal(stages.completion_drive_ns, (uint64_t)20u);

    stages.operations = UINT64_MAX;
    check_equal(network_stage_measurement_add(&stages, 1u, 1u), TURBO_ERANGE);
    check_equal(stages.operations, UINT64_MAX);
    check_equal(stages.admission_ns, (uint64_t)10u);
    check_equal(stages.completion_drive_ns, (uint64_t)20u);

    stages.operations = 1u;
    stages.admission_ns = UINT64_MAX;
    check_equal(network_stage_measurement_add(&stages, 1u, 1u), TURBO_ERANGE);
    check_equal(stages.operations, (uint64_t)1u);
    check_equal(stages.admission_ns, UINT64_MAX);
    check_equal(stages.completion_drive_ns, (uint64_t)20u);
  }

  it("names drivers and normalizes stage sums by completed IO operations") {
    network_stage_measurement stages = {
        .enabled = true, .operations = 4u, .admission_ns = 40u, .completion_drive_ns = 100u};

    check_equal(strcmp(network_driver_mode_name(NETWORK_DRIVER_ACTOR), "actor"), 0);
    check_equal(strcmp(network_driver_mode_name(NETWORK_DRIVER_SOURCE), "source"), 0);
    check_equal(strcmp(network_driver_mode_name(NETWORK_DRIVER_DIRECT), "direct"), 0);
    check_equal(strcmp(network_driver_mode_name(NETWORK_DRIVER_VECTOR), "vector"), 0);
    check_true(network_stage_mean_ns(stages.admission_ns, stages.operations) > 9.999);
    check_true(network_stage_mean_ns(stages.admission_ns, stages.operations) < 10.001);
    check_true(network_stage_mean_ns(stages.completion_drive_ns, stages.operations) > 24.999);
    check_true(network_stage_mean_ns(stages.completion_drive_ns, stages.operations) < 25.001);
    check_true(network_stage_mean_ns(1u, 0u) == 0.0);
  }

  it("parses blocking and busy wait modes and rejects unknown input") {
    network_wait_mode mode = NETWORK_WAIT_BUSY;

    check_equal(network_parse_wait_mode(NULL, &mode), TURBO_OK);
    check_equal(mode, NETWORK_WAIT_BLOCKING);
    check_equal(network_parse_wait_mode("blocking", &mode), TURBO_OK);
    check_equal(mode, NETWORK_WAIT_BLOCKING);
    check_equal(network_parse_wait_mode("busy", &mode), TURBO_OK);
    check_equal(mode, NETWORK_WAIT_BUSY);
    check_equal(network_parse_wait_mode("spin", &mode), TURBO_EINVAL);
    check_equal(network_parse_wait_mode("blocking", NULL), TURBO_EINVAL);
  }

  it("normalizes application throughput by process CPU time") {
    const double one_mib_per_second =
        network_application_mib_per_second(UINT64_C(1048576), UINT64_C(1000000000));
    const double two_mib_per_cpu_second =
        network_application_mib_per_cpu_second(UINT64_C(1048576), UINT64_C(500000000));

    check_true(one_mib_per_second > 0.999999);
    check_true(one_mib_per_second < 1.000001);
    check_true(two_mib_per_cpu_second > 1.999999);
    check_true(two_mib_per_cpu_second < 2.000001);
    check_true(network_application_mib_per_second(1u, 0u) == 0.0);
    check_true(network_application_mib_per_cpu_second(1u, 0u) == 0.0);
    check_true(network_exchanges_per_second(2000u, UINT64_C(1000000000)) > 1999.999);
    check_true(network_exchanges_per_second(2000u, UINT64_C(1000000000)) < 2000.001);
    check_true(network_exchanges_per_second(1u, 0u) == 0.0);
  }
}

suite("CFlow network benchmarks") {
  bench("reports loopback TCP or UDP driver performance") {
    const char *protocol_text = getenv("CFLOW_NETWORK_PROTOCOL");
    const char *profile_text = getenv("CFLOW_NETWORK_PROFILE");
    const char *workload_text = getenv("CFLOW_NETWORK_WORKLOAD");
    const char *wait_mode_text = getenv("CFLOW_NETWORK_WAIT_MODE");
    const char *peer_mode_text = getenv("CFLOW_NETWORK_PEER");
    const char *driver_mode_text = getenv("CFLOW_NETWORK_DRIVER");
    const char *source_window_text = getenv("CFLOW_NETWORK_SOURCE_WINDOW");
    const char *stage_timing_text = getenv("CFLOW_NETWORK_STAGE_TIMING");
    network_protocol protocol = NETWORK_PROTOCOL_TCP;
    network_wait_mode wait_mode = NETWORK_WAIT_BLOCKING;
    network_peer_mode peer_mode = NETWORK_PEER_RAW;
    network_driver_mode driver_mode = NETWORK_DRIVER_ACTOR;
    network_workload workload = NETWORK_WORKLOAD_ROUND_TRIP;
    bool stage_timing = false;
    bool throughput = false;
    cflow_io_native_backend_kind backend_kind = CFLOW_IO_NATIVE_EPOLL;
    int config_status = network_parse_protocol(protocol_text, &protocol);
    if (config_status == TURBO_OK) config_status = network_parse_profile(profile_text, &throughput);
    if (config_status == TURBO_OK)
      config_status = network_parse_workload(workload_text, &workload);
    if (config_status == TURBO_OK)
      config_status = network_parse_wait_mode(wait_mode_text, &wait_mode);
    if (config_status == TURBO_OK)
      config_status = network_parse_peer_mode(peer_mode_text, &peer_mode);
    if (config_status == TURBO_OK)
      config_status = network_parse_driver_mode(driver_mode_text, &driver_mode);
    if (config_status == TURBO_OK)
      config_status = network_parse_stage_timing(stage_timing_text, &stage_timing);
    if (config_status == TURBO_OK)
      config_status =
          network_validate_configuration(driver_mode, peer_mode, wait_mode, stage_timing);
    if (config_status == TURBO_OK)
      config_status = network_validate_workload(workload, protocol, driver_mode,
                                                peer_mode, throughput);
    if (config_status == TURBO_OK && driver_mode == NETWORK_DRIVER_VECTOR &&
        protocol != NETWORK_PROTOCOL_TCP)
      config_status = TURBO_ENOTSUP;
    if (config_status == TURBO_OK && driver_mode != NETWORK_DRIVER_DIRECT)
      config_status = network_select_backend(&backend_kind);
    const size_t source_window_capacity = network_env_size(
        "CFLOW_NETWORK_SOURCE_WINDOW", 1u, NETWORK_SOURCE_MAX_WINDOW);
    if (config_status == TURBO_OK &&
        (source_window_capacity == 0u ||
         (driver_mode != NETWORK_DRIVER_SOURCE && source_window_text != NULL &&
          source_window_capacity != 1u)))
      config_status = TURBO_EINVAL;
    const size_t samples = network_env_size(
        "CFLOW_NETWORK_SAMPLES", throughput ? NETWORK_THROUGHPUT_SAMPLES : NETWORK_LATENCY_SAMPLES,
        NETWORK_MAX_SAMPLES);
    const size_t exchanges =
        network_env_size("CFLOW_NETWORK_EXCHANGES",
                         throughput ? NETWORK_THROUGHPUT_EXCHANGES : NETWORK_LATENCY_EXCHANGES,
                         NETWORK_MAX_EXCHANGES);
    const size_t payload_size = network_env_size(
        "CFLOW_NETWORK_PAYLOAD",
        throughput ? (protocol == NETWORK_PROTOCOL_UDP ? NETWORK_UDP_THROUGHPUT_PAYLOAD
                                                       : NETWORK_TCP_THROUGHPUT_PAYLOAD)
                   : NETWORK_LATENCY_PAYLOAD,
        protocol == NETWORK_PROTOCOL_UDP ? NETWORK_UDP_MAX_PAYLOAD : NETWORK_TCP_MAX_PAYLOAD);
    const size_t total_exchanges =
        samples != 0u && exchanges <= SIZE_MAX / samples ? samples * exchanges : 0u;
    const size_t bytes_per_sample =
        payload_size != 0u && exchanges <= SIZE_MAX / payload_size ? exchanges * payload_size : 0u;
    const size_t receive_buffer_size =
        workload == NETWORK_WORKLOAD_PIPELINE
            ? (payload_size != 0u && source_window_capacity <= SIZE_MAX / payload_size
                   ? source_window_capacity * payload_size
                   : 0u)
            : payload_size;
    const uint64_t application_bytes = (uint64_t)total_exchanges * (uint64_t)payload_size;
    network_fixture fixture;
    network_measurement measured = {0};
    cflow_io_actor_stats actor_stats = {0};
    cflow_io_native_backend_stats native_stats = {0};
    cflow_io_actor_stats server_actor_stats = {0};
    cflow_io_native_backend_stats server_native_stats = {0};
    cflow_io_source_window_stats source_window_stats = {0};
    unsigned char *sent = NULL;
    unsigned char *received = NULL;
    uint64_t wall_started = 0u;
    uint64_t cpu_started = 0u;
    int run_status = TURBO_OK;
    int server_join_status = TURBO_OK;
    int server_status = TURBO_OK;
    int cleanup_status = TURBO_OK;
    bool actor_stats_ok = false;
    bool native_stats_ok = false;
    bool server_actor_stats_ok = peer_mode == NETWORK_PEER_RAW;
    bool server_native_stats_ok = peer_mode == NETWORK_PEER_RAW;
    bool source_window_stats_ok = driver_mode != NETWORK_DRIVER_SOURCE;
    char title[112];

    check_equal(config_status, TURBO_OK);
    check_true(samples != 0u && exchanges != 0u && payload_size != 0u && total_exchanges != 0u &&
               bytes_per_sample != 0u && receive_buffer_size != 0u);
    check_true(driver_mode == NETWORK_DRIVER_DIRECT ||
               cflow_io_native_backend_supported(backend_kind));
    measured.latencies = (uint64_t *)calloc(total_exchanges, sizeof(*measured.latencies));
    sent = (unsigned char *)malloc(payload_size);
    received = (unsigned char *)malloc(receive_buffer_size);
    check_not_null(measured.latencies);
    check_not_null(sent);
    check_not_null(received);
    measured.latency_capacity = total_exchanges;
    memset(sent, 0x5a, payload_size);
    check_equal(network_fixture_init_with_driver_window(
                    &fixture, protocol, backend_kind, wait_mode, peer_mode,
                    driver_mode, source_window_capacity, total_exchanges,
                    payload_size),
                TURBO_OK);
    fixture.stages.enabled = stage_timing;
    if (driver_mode == NETWORK_DRIVER_SOURCE)
      (void)snprintf(title, sizeof(title), "%s-%s-%s-%s-%s-source-w%zu-%s",
                     protocol == NETWORK_PROTOCOL_TCP ? "tcp" : "udp",
                     throughput ? "throughput" : "latency", network_backend_name(backend_kind),
                     network_wait_mode_name(wait_mode), network_peer_mode_name(peer_mode),
                     source_window_capacity, network_workload_name(workload));
    else if (driver_mode == NETWORK_DRIVER_DIRECT)
      (void)snprintf(title, sizeof(title), "%s-%s-socket-%s-%s-direct",
                     protocol == NETWORK_PROTOCOL_TCP ? "tcp" : "udp",
                     throughput ? "throughput" : "latency", network_wait_mode_name(wait_mode),
                      network_peer_mode_name(peer_mode));
    else if (driver_mode == NETWORK_DRIVER_VECTOR)
      (void)snprintf(title, sizeof(title), "%s-%s-%s-%s-%s-vector",
                     protocol == NETWORK_PROTOCOL_TCP ? "tcp" : "udp",
                     throughput ? "throughput" : "latency", network_backend_name(backend_kind),
                     network_wait_mode_name(wait_mode), network_peer_mode_name(peer_mode));
    else
      (void)snprintf(title, sizeof(title), "%s-%s-%s-%s-%s",
                     protocol == NETWORK_PROTOCOL_TCP ? "tcp" : "udp",
                     throughput ? "throughput" : "latency", network_backend_name(backend_kind),
                     network_wait_mode_name(wait_mode), network_peer_mode_name(peer_mode));
    wall_started = turbo_hrtime();
    cpu_started = network_process_cpu_ns();
    benchmark_io(title, samples, exchanges, bytes_per_sample) {
      if (run_status == TURBO_OK) {
        if (workload == NETWORK_WORKLOAD_PIPELINE) {
          for (size_t exchange = 0u; exchange < exchanges;) {
            const size_t remaining = exchanges - exchange;
            const size_t batch = remaining < source_window_capacity
                                     ? remaining
                                     : source_window_capacity;
            uint64_t batch_latencies[NETWORK_SOURCE_MAX_WINDOW] = {0};
            run_status = network_exchange_source_pipeline_batch(
                &fixture, sent, received, payload_size, batch,
                batch_latencies);
            if (run_status != TURBO_OK) break;
            for (size_t index = 0u; index < batch; ++index)
              measured.latencies[measured.latency_count++] =
                  batch_latencies[index];
            exchange += batch;
          }
        } else {
          for (size_t exchange = 0u; exchange < exchanges; ++exchange) {
            const uint64_t started = turbo_hrtime();
            run_status = network_exchange(
                &fixture, protocol, sent, received, payload_size,
                (cflow_io_lease_id)(measured.latency_count *
                                        (peer_mode == NETWORK_PEER_NATIVE ? 4u : 2u) +
                                    1u));
            if (run_status != TURBO_OK) break;
            measured.latencies[measured.latency_count++] = turbo_hrtime() - started;
          }
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
    if (driver_mode == NETWORK_DRIVER_DIRECT) {
      actor_stats_ok = true;
      native_stats_ok = true;
    } else {
      actor_stats_ok = network_endpoint_get_actor_stats(&fixture.client, &actor_stats);
      native_stats_ok = cflow_io_native_backend_get_stats(&fixture.client.backend, &native_stats);
      if (driver_mode == NETWORK_DRIVER_SOURCE)
        source_window_stats_ok = cflow_io_source_owner_get_window_stats(
            &fixture.client.source.owner, &source_window_stats);
    }
    if (peer_mode == NETWORK_PEER_NATIVE) {
      server_actor_stats_ok =
          cflow_io_actor_get_stats(&fixture.native_server.actor, &server_actor_stats);
      server_native_stats_ok =
          cflow_io_native_backend_get_stats(&fixture.native_server.backend, &server_native_stats);
    }
    cleanup_status = network_fixture_destroy(&fixture);
    if (server_join_status == TURBO_OK && server_status == TURBO_OK && cleanup_status == TURBO_OK &&
        run_status == TURBO_OK && measured.latency_count == total_exchanges && actor_stats_ok &&
        native_stats_ok && server_actor_stats_ok && server_native_stats_ok &&
        source_window_stats_ok) {
      qsort(measured.latencies, measured.latency_count, sizeof(*measured.latencies),
            network_u64_compare);
      printf(
          "CFLOW_BENCHMARK_JSON {\"schema\":\"cflow-network-benchmark/v1\","
          "\"protocol\":\"%s\",\"profile\":\"%s\",\"backend\":\"%s\","
          "\"workload\":\"%s\",\"wait_mode\":\"%s\",\"peer_mode\":\"%s\","
          "\"driver\":\"%s\","
          "\"stage_timing\":%s,\"source_window_capacity\":%zu,"
          "\"source_peak_occupied\":%zu,"
          "\"samples\":%zu,\"exchanges_per_sample\":%zu,"
          "\"payload_bytes\":%zu,\"application_bytes\":%" PRIu64 ","
          "\"wire_bytes\":%" PRIu64 ",\"wall_ns\":%" PRIu64 ","
          "\"io_operations\":%" PRIu64 ",\"admission_ns\":%" PRIu64 ","
          "\"completion_drive_ns\":%" PRIu64 ",\"admission_mean_ns\":%.3f,"
          "\"completion_drive_mean_ns\":%.3f,"
          "\"process_cpu_ns\":%" PRIu64 ",\"process_cpu_pct\":%.3f,"
          "\"cpu_core_equivalents\":%.6f,"
          "\"exchanges_per_second\":%.3f,"
          "\"application_mib_per_second\":%.6f,"
          "\"application_mib_per_cpu_second\":%.6f,"
          "\"peak_rss_bytes\":%" PRIu64 ",\"p50_ns\":%" PRIu64 ","
          "\"p95_ns\":%" PRIu64 ",\"p99_ns\":%" PRIu64 ","
          "\"attempted\":%zu,\"errors\":0,\"rejections\":%" PRIu64 ","
          "\"stale_completions\":%" PRIu64 "}\n",
          protocol == NETWORK_PROTOCOL_TCP ? "tcp" : "udp", throughput ? "throughput" : "latency",
          network_result_backend_name(driver_mode, backend_kind), network_workload_name(workload),
          network_wait_mode_name(wait_mode), network_peer_mode_name(peer_mode),
          network_driver_mode_name(driver_mode),
          stage_timing ? "true" : "false",
          driver_mode == NETWORK_DRIVER_SOURCE ? source_window_stats.capacity : 0u,
          driver_mode == NETWORK_DRIVER_SOURCE ? source_window_stats.peak_occupied : 0u,
          samples, exchanges, payload_size, application_bytes,
          application_bytes * UINT64_C(2), measured.wall_ns, fixture.stages.operations,
          fixture.stages.admission_ns, fixture.stages.completion_drive_ns,
          network_stage_mean_ns(fixture.stages.admission_ns, fixture.stages.operations),
          network_stage_mean_ns(fixture.stages.completion_drive_ns, fixture.stages.operations),
          measured.cpu_ns,
          measured.wall_ns != 0u ? (double)measured.cpu_ns * 100.0 / (double)measured.wall_ns : 0.0,
          measured.wall_ns != 0u ? (double)measured.cpu_ns / (double)measured.wall_ns : 0.0,
          network_exchanges_per_second(total_exchanges, measured.wall_ns),
          network_application_mib_per_second(application_bytes, measured.wall_ns),
          network_application_mib_per_cpu_second(application_bytes, measured.cpu_ns),
          measured.peak_rss_bytes,
          network_percentile(measured.latencies, measured.latency_count, 50u),
          network_percentile(measured.latencies, measured.latency_count, 95u),
          network_percentile(measured.latencies, measured.latency_count, 99u), total_exchanges,
          actor_stats.rejected_request_full + actor_stats.rejected_command_full +
              actor_stats.rejected_closed + actor_stats.rejected_lease_in_use +
              actor_stats.executor_rejected_full + actor_stats.executor_rejected_closed +
              actor_stats.executor_rejected_invalid + native_stats.rejected_full +
              server_actor_stats.rejected_request_full + server_actor_stats.rejected_command_full +
              server_actor_stats.rejected_closed + server_actor_stats.rejected_lease_in_use +
              server_actor_stats.executor_rejected_full +
              server_actor_stats.executor_rejected_closed +
              server_actor_stats.executor_rejected_invalid + server_native_stats.rejected_full,
          actor_stats.stale_completions + native_stats.stale_native_completions +
              server_actor_stats.stale_completions + server_native_stats.stale_native_completions);
    }
    if (cleanup_status == TURBO_OK) {
      free(received);
      free(sent);
    }
    free(measured.latencies);
    check_equal(server_join_status, TURBO_OK);
    check_equal(server_status, TURBO_OK);
    check_equal(cleanup_status, TURBO_OK);
    check_equal(run_status, TURBO_OK);
    check_equal(measured.latency_count, total_exchanges);
    check_true(actor_stats_ok);
    check_true(native_stats_ok);
    check_true(server_actor_stats_ok);
    check_true(server_native_stats_ok);
    check_true(source_window_stats_ok);
  }
}
