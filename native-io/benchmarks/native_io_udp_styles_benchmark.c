#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/native_io.h>
#include <salts/native_io_sharded.h>

#include <inttypes.h>
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
typedef SOCKET udp_style_socket;
typedef int udp_style_socklen;
  #define UDP_STYLE_INVALID_SOCKET INVALID_SOCKET
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <netinet/in.h>
    #include <sys/socket.h>
  #include <unistd.h>
typedef int udp_style_socket;
typedef socklen_t udp_style_socklen;
  #define UDP_STYLE_INVALID_SOCKET (-1)
#endif

enum {
  UDP_STYLE_WARMUP_TRANSFERS = 32,
  UDP_STYLE_MEASURED_TRANSFERS = 512,
  UDP_STYLE_TIMEOUT_MS = 5000,
  UDP_STYLE_REQUEST_CAPACITY = 4,
  UDP_STYLE_COMPLETION_CAPACITY = 4,
  UDP_STYLE_SHARDED_QUEUE_CAPACITY = 64
};

static const size_t UDP_STYLE_PAYLOADS[] = {1024u, 4096u, 8192u};

typedef enum udp_style_kind {
  UDP_STYLE_DIRECT = 0,
  UDP_STYLE_COROUTINE,
  UDP_STYLE_SHARDED_SAME_OWNER,
  UDP_STYLE_SHARDED_CROSS_OWNER
} udp_style_kind;

typedef struct udp_style_result {
  const char *style;
  size_t payload_size;
  size_t transfers;
  uint64_t wall_ns;
  uint64_t p50_ns;
  uint64_t p95_ns;
  double mib_per_second;
  double message_hops_per_transfer;
  double same_owner_direct_tasks_per_transfer;
  uint64_t queued_dispatches;
  uint64_t rejected_tasks;
  uint64_t peak_command_slots;
} udp_style_result;

typedef struct udp_style_backend_fixture {
  native_io_backend backend;
  native_io_endpoint endpoints[2];
  udp_style_socket sockets[2];
  struct sockaddr_in addresses[2];
  struct sockaddr_storage peer_address;
  unsigned char *sent;
  unsigned char *received;
  size_t payload_size;
} udp_style_backend_fixture;

typedef struct udp_style_coroutine_operation {
  udp_style_backend_fixture *fixture;
  native_io_completion completion;
  int status;
  bool send;
  bool done;
} udp_style_coroutine_operation;

typedef struct udp_style_sharded_fixture {
  native_io_sharded *runtime;
  native_io_sharded_endpoint endpoints[2];
  udp_style_socket sockets[2];
  struct sockaddr_in addresses[2];
  unsigned char *sent;
  unsigned char *received;
  size_t payload_size;
  int attach_status;
  int release_status;
} udp_style_sharded_fixture;

typedef struct udp_style_sharded_operation {
  int admission_status;
  native_io_sharded_request request;
  native_io_completion_kind kind;
  int terminal_status;
  size_t bytes;
  uintptr_t user_data;
  size_t address_length;
  size_t expected_owner_shard;
  size_t admission_shard;
  size_t terminal_shard;
  struct sockaddr_in destination;
  struct sockaddr_storage peer_address;
  unsigned terminal_count;
  unsigned finalize_count;
} udp_style_sharded_operation;

typedef struct udp_style_sharded_observe {
  native_io_sharded_completion events[UDP_STYLE_COMPLETION_CAPACITY];
  size_t count;
  int status;
} udp_style_sharded_observe;

typedef struct udp_style_same_driver {
  udp_style_sharded_fixture *fixture;
  uint64_t *latencies;
  native_io_sharded_stats before;
  native_io_sharded_stats after;
  int status;
} udp_style_same_driver;

static native_io_backend_kind udp_style_backend(void) {
  const char *value = getenv("NATIVE_IO_UDP_STYLE_BACKEND");
  if (value == NULL || *value == '\0') {
#if defined(_WIN32)
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
    return NATIVE_IO_BACKEND_EPOLL;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
    defined(__DragonFly__)
    return NATIVE_IO_BACKEND_KQUEUE;
#else
    return (native_io_backend_kind)0;
#endif
  }
  if (strcmp(value, "epoll") == 0) return NATIVE_IO_BACKEND_EPOLL;
  if (strcmp(value, "io_uring") == 0) return NATIVE_IO_BACKEND_IO_URING;
  if (strcmp(value, "iocp") == 0) return NATIVE_IO_BACKEND_IOCP;
  if (strcmp(value, "kqueue") == 0) return NATIVE_IO_BACKEND_KQUEUE;
  return (native_io_backend_kind)0;
}

static const char *udp_style_backend_name(native_io_backend_kind kind) {
  switch (kind) {
    case NATIVE_IO_BACKEND_EPOLL: return "epoll";
    case NATIVE_IO_BACKEND_IO_URING: return "io_uring";
    case NATIVE_IO_BACKEND_IOCP: return "iocp";
    case NATIVE_IO_BACKEND_KQUEUE: return "kqueue";
    default: return "unsupported";
  }
}

static const char *udp_style_name(udp_style_kind style) {
  switch (style) {
    case UDP_STYLE_DIRECT: return "direct";
    case UDP_STYLE_COROUTINE: return "coroutine";
    case UDP_STYLE_SHARDED_SAME_OWNER: return "sharded_same_owner";
    case UDP_STYLE_SHARDED_CROSS_OWNER: return "sharded_cross_owner";
    default: return "unknown";
  }
}

static int udp_style_socket_error(void) {
#if defined(_WIN32)
  const int error = WSAGetLastError();
#else
  const int error = errno;
#endif
  return error == 0 ? SALTS_EIO : -error;
}

static int udp_style_network_start(void) {
#if defined(_WIN32)
  WSADATA data;
  const int status = WSAStartup(MAKEWORD(2, 2), &data);
  return status == 0 ? SALTS_OK : -status;
#else
  return SALTS_OK;
#endif
}

static void udp_style_network_stop(void) {
#if defined(_WIN32)
  (void)WSACleanup();
#endif
}

static void udp_style_close_socket(udp_style_socket socket_value) {
  if (socket_value == UDP_STYLE_INVALID_SOCKET) return;
#if defined(_WIN32)
  (void)closesocket(socket_value);
#else
  (void)close(socket_value);
#endif
}

static int udp_style_set_nonblocking(udp_style_socket socket_value) {
#if defined(_WIN32)
  u_long enabled = 1u;
  return ioctlsocket(socket_value, FIONBIO, &enabled) == 0
             ? SALTS_OK
             : udp_style_socket_error();
#else
  const int flags = fcntl(socket_value, F_GETFL, 0);
  if (flags < 0) return udp_style_socket_error();
  if ((flags & O_NONBLOCK) != 0) return SALTS_OK;
  return fcntl(socket_value, F_SETFL, flags | O_NONBLOCK) == 0
             ? SALTS_OK
             : udp_style_socket_error();
#endif
}

static int udp_style_bind_loopback(udp_style_socket socket_value,
                                   struct sockaddr_in *address) {
  udp_style_socklen address_length = (udp_style_socklen)sizeof(*address);
  memset(address, 0, sizeof(*address));
  address->sin_family = AF_INET;
  address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address->sin_port = 0u;
  if (bind(socket_value, (const struct sockaddr *)address,
           (udp_style_socklen)sizeof(*address)) != 0)
    return udp_style_socket_error();
  if (getsockname(socket_value, (struct sockaddr *)address, &address_length) != 0)
    return udp_style_socket_error();
  return SALTS_OK;
}

static int udp_style_make_pair(udp_style_socket sockets[2],
                               struct sockaddr_in addresses[2]) {
  int status = SALTS_OK;
  sockets[0] = UDP_STYLE_INVALID_SOCKET;
  sockets[1] = UDP_STYLE_INVALID_SOCKET;
  sockets[0] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sockets[0] == UDP_STYLE_INVALID_SOCKET) return udp_style_socket_error();
  sockets[1] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sockets[1] == UDP_STYLE_INVALID_SOCKET) status = udp_style_socket_error();
  if (status == SALTS_OK) status = udp_style_bind_loopback(sockets[0], &addresses[0]);
  if (status == SALTS_OK) status = udp_style_bind_loopback(sockets[1], &addresses[1]);
  if (status == SALTS_OK) status = udp_style_set_nonblocking(sockets[0]);
  if (status == SALTS_OK) status = udp_style_set_nonblocking(sockets[1]);
  if (status != SALTS_OK) {
    udp_style_close_socket(sockets[0]);
    udp_style_close_socket(sockets[1]);
    sockets[0] = UDP_STYLE_INVALID_SOCKET;
    sockets[1] = UDP_STYLE_INVALID_SOCKET;
  }
  return status;
}

static int udp_style_compare_u64static int udp_style_compare_u64(const void *left, const void *right) {
  const uint64_t a = *(const uint64_t *)left;
  const uint64_t b = *(const uint64_t *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static int udp_style_finalize_result(udp_style_result *result, uint64_t *latencies) {
  if (result == NULL || latencies == NULL || result->transfers == 0u) return SALTS_EINVAL;
  qsort(latencies, result->transfers, sizeof(*latencies), udp_style_compare_u64);
  result->p50_ns = latencies[(result->transfers - 1u) * 50u / 100u];
  result->p95_ns = latencies[(result->transfers - 1u) * 95u / 100u];
  result->mib_per_second =
      result->wall_ns == 0u
          ? 0.0
          : (double)result->payload_size * (double)result->transfers * 1.0e9 /
                (double)result->wall_ns / (1024.0 * 1024.0);
  return SALTS_OK;
}

static int udp_style_backend_fixture_init(udp_style_backend_fixture *fixture,
                                            native_io_backend_kind kind,
                                            size_t payload_size) {
  const native_io_backend_config config = {
      kind, 2u, UDP_STYLE_REQUEST_CAPACITY, UDP_STYLE_COMPLETION_CAPACITY};
  int status;

  memset(fixture, 0, sizeof(*fixture));
  fixture->sockets[0] = UDP_STYLE_INVALID_SOCKET;
  fixture->sockets[1] = UDP_STYLE_INVALID_SOCKET;
  fixture->payload_size = payload_size;
  fixture->sent = (unsigned char *)malloc(payload_size);
  fixture->received = (unsigned char *)malloc(payload_size);
  if (fixture->sent == NULL || fixture->received == NULL) return SALTS_ENOMEM;
  memset(fixture->sent, 0x5au, payload_size);
  memset(fixture->received, 0, payload_size);
  memset(&fixture->peer_address, 0, sizeof(fixture->peer_address));

  status = udp_style_make_pair(fixture->sockets, fixture->addresses);
  if (status != SALTS_OK) return status;
  status = native_io_backend_init(&fixture->backend, &config);
  if (status == SALTS_OK)
    status = native_io_backend_attach_socket(&fixture->backend,
                                             (uintptr_t)fixture->sockets[0],
                                             &fixture->endpoints[0]);
  if (status == SALTS_OK)
    status = native_io_backend_attach_socket(&fixture->backend,
                                             (uintptr_t)fixture->sockets[1],
                                             &fixture->endpoints[1]);
  return status;
}

static int udp_style_backend_fixture_destroystatic int udp_style_backend_fixture_destroy(udp_style_backend_fixture *fixture) {
  int status = SALTS_OK;
#if defined(_WIN32)
  int backend_close_status = SALTS_OK;
  if (fixture->backend.impl != NULL) {
    backend_close_status = native_io_backend_close(&fixture->backend);
    if (backend_close_status != SALTS_OK && backend_close_status != SALTS_EALREADY)
      status = backend_close_status;
  }
#endif

  for (size_t index = 0u; index < 2u; ++index) {
    if (fixture->sockets[index] != UDP_STYLE_INVALID_SOCKET) {
      udp_style_close_socket(fixture->sockets[index]);
      fixture->sockets[index] = UDP_STYLE_INVALID_SOCKET;
    }
  }

  if (fixture->backend.impl != NULL) {
    for (size_t index = 0u; index < 2u; ++index) {
      if (native_io_endpoint_valid(fixture->endpoints[index])) {
        const int release_status =
            native_io_backend_release_socket(&fixture->backend, fixture->endpoints[index]);
        if (status == SALTS_OK && release_status != SALTS_OK) status = release_status;
        fixture->endpoints[index] = (native_io_endpoint){0};
      }
    }
    {
#if defined(_WIN32)
      const int close_status = backend_close_status;
#else
      const int close_status = native_io_backend_close(&fixture->backend);
#endif
      const int destroy_status =
          close_status == SALTS_OK || close_status == SALTS_EALREADY
              ? native_io_backend_destroy(&fixture->backend)
              : close_status;
      if (status == SALTS_OK && destroy_status != SALTS_OK) status = destroy_status;
    }
  }

  free(fixture->received);
  free(fixture->sent);
  fixture->received = NULL;
  fixture->sent = NULL;
  return status;
}

static int udp_style_validate_peer(const struct sockaddr_storage *storage,
                                   size_t address_length,
                                   const struct sockaddr_in *expected) {
  const struct sockaddr_in *peer = (const struct sockaddr_in *)storage;
  if (storage == NULL || expected == NULL) return SALTS_EINVAL;
  if (address_length != sizeof(*peer) || peer->sin_family != AF_INET ||
      peer->sin_port != expected->sin_port ||
      peer->sin_addr.s_addr != expected->sin_addr.s_addr)
    return SALTS_EPROTO;
  return SALTS_OK;
}

static int udp_style_direct_transfer(udp_style_backend_fixture *fixture) {
  native_io_completion events[UDP_STYLE_COMPLETION_CAPACITY];
  native_io_request requests[2] = {0};
  size_t total = 0u;
  bool saw_receive = false;
  bool saw_send = false;
  int status;

  memset(fixture->received, 0, fixture->payload_size);
  memset(&fixture->peer_address, 0, sizeof(fixture->peer_address));

  const native_io_operation receive_operation = {
      .kind = NATIVE_IO_OPERATION_UDP_RECV_FROM,
      .endpoint = fixture->endpoints[1],
      .buffer = fixture->received,
      .length = fixture->payload_size,
      .user_data = 1u,
      .address = &fixture->peer_address,
      .address_capacity = sizeof(fixture->peer_address)};
  const native_io_operation send_operation = {
      .kind = NATIVE_IO_OPERATION_UDP_SEND_TO,
      .endpoint = fixture->endpoints[0],
      .buffer = fixture->sent,
      .length = fixture->payload_size,
      .user_data = 2u,
      .address = &fixture->addresses[1],
      .address_capacity = sizeof(fixture->addresses[1]),
      .address_length = sizeof(fixture->addresses[1])};

  status = native_io_backend_submit(&fixture->backend, &receive_operation, &requests[0]);
  if (status != SALTS_OK) return status;
  status = native_io_backend_submit(&fixture->backend, &send_operation, &requests[1]);
  if (status != SALTS_OK) return status;

  while (total < 2u) {
    size_t count = 0u;
    status = native_io_backend_observe(&fixture->backend, events + total,
                                       2u - total, UDP_STYLE_TIMEOUT_MS, &count);
    if (status != SALTS_OK) return status;
    if (count == 0u) return SALTS_EIO;
    total += count;
  }

  for (size_t index = 0u; index < total; ++index) {
    const native_io_completion *event = &events[index];
    if (event->kind != NATIVE_IO_COMPLETION_OK ||
        event->bytes != fixture->payload_size)
      return event->status != SALTS_OK ? event->status : SALTS_EIO;
    if (event->user_data == 1u) {
      status = udp_style_validate_peer(&fixture->peer_address,
                                       event->address_length,
                                       &fixture->addresses[0]);
      if (status != SALTS_OK) return status;
      saw_receive = true;
    } else if (event->user_data == 2u) {
      if (event->address_length != 0u) return SALTS_EPROTO;
      saw_send = true;
    } else {
      return SALTS_EPROTO;
    }
  }

  if (!saw_receive || !saw_send) return SALTS_EPROTO;
  return memcmp(fixture->sent, fixture->received, fixture->payload_size) == 0
             ? SALTS_OK
             : SALTS_EIO;
}

static void udp_style_coroutine_entry(native_io_coroutine *coroutine, void *arg) {
  udp_style_coroutine_operation *state = (udp_style_coroutine_operation *)arg;
  udp_style_backend_fixture *fixture = state->fixture;
  native_io_operation operation = {0};

  state->status = SALTS_OK;
  operation.kind = state->send ? NATIVE_IO_OPERATION_UDP_SEND_TO
                               : NATIVE_IO_OPERATION_UDP_RECV_FROM;
  operation.endpoint = fixture->endpoints[state->send ? 0u : 1u];
  operation.buffer = state->send ? fixture->sent : fixture->received;
  operation.length = fixture->payload_size;
  operation.user_data = state->send ? 2u : 1u;
  if (state->send) {
    operation.address = &fixture->addresses[1];
    operation.address_capacity = sizeof(fixture->addresses[1]);
    operation.address_length = sizeof(fixture->addresses[1]);
  } else {
    operation.address = &fixture->peer_address;
    operation.address_capacity = sizeof(fixture->peer_address);
  }

  state->status =
      native_io_coroutine_await(coroutine, &operation, &state->completion);
  state->done = true;
}

static int udp_style_coroutine_cancel_and_drainstatic int udp_style_coroutine_cancel_and_drain(
    udp_style_backend_fixture *fixture, native_io_coroutine_task task,
    udp_style_coroutine_operation *state) {
  native_io_completion events[UDP_STYLE_COMPLETION_CAPACITY];
  int status;

  if (state->done || !native_io_coroutine_task_valid(task)) return SALTS_OK;
  status = native_io_backend_cancel_coroutine(&fixture->backend, task);
  if (status != SALTS_OK && status != SALTS_EALREADY) return status;
  while (!state->done) {
    size_t count = 0u;
    status = native_io_backend_observe(&fixture->backend, events,
                                       UDP_STYLE_COMPLETION_CAPACITY,
                                       UDP_STYLE_TIMEOUT_MS, &count);
    if (status != SALTS_OK) return status;
    if (count != 0u) return SALTS_EPROTO;
  }
  return SALTS_OK;
}

static int udp_style_coroutine_transfer(udp_style_backend_fixture *fixture) {
  udp_style_coroutine_operation receive_state = {
      .fixture = fixture, .status = SALTS_OK, .send = false, .done = false};
  udp_style_coroutine_operation send_state = {
      .fixture = fixture, .status = SALTS_OK, .send = true, .done = false};
  native_io_coroutine_task receive_task = {0};
  native_io_coroutine_task send_task = {0};
  native_io_completion events[UDP_STYLE_COMPLETION_CAPACITY];
  int status;

  memset(fixture->received, 0, fixture->payload_size);
  memset(&fixture->peer_address, 0, sizeof(fixture->peer_address));
  status = native_io_backend_spawn_coroutine(
      &fixture->backend, udp_style_coroutine_entry,
      &receive_state, &receive_task);
  if (status == SALTS_OK)
    status = native_io_backend_spawn_coroutine(
        &fixture->backend, udp_style_coroutine_entry,
        &send_state, &send_task);

  while (status == SALTS_OK && (!receive_state.done || !send_state.done)) {
    size_t count = 0u;
    status = native_io_backend_observe(
        &fixture->backend, events, UDP_STYLE_COMPLETION_CAPACITY,
        UDP_STYLE_TIMEOUT_MS, &count);
    if (status == SALTS_OK && count != 0u) status = SALTS_EPROTO;
  }

  if (status != SALTS_OK) {
    const int failure = status;
    const int send_drain =
        udp_style_coroutine_cancel_and_drain(fixture, send_task, &send_state);
    const int receive_drain =
        udp_style_coroutine_cancel_and_drain(fixture, receive_task, &receive_state);
    if (send_drain != SALTS_OK) return send_drain;
    if (receive_drain != SALTS_OK) return receive_drain;
    return failure;
  }
  if (receive_state.status != SALTS_OK) return receive_state.status;
  if (send_state.status != SALTS_OK) return send_state.status;
  if (receive_state.completion.kind != NATIVE_IO_COMPLETION_OK ||
      receive_state.completion.bytes != fixture->payload_size ||
      receive_state.completion.user_data != 1u)
    return SALTS_EIO;
  status = udp_style_validate_peer(
      &fixture->peer_address, receive_state.completion.address_length,
      &fixture->addresses[0]);
  if (status != SALTS_OK) return status;
  if (send_state.completion.kind != NATIVE_IO_COMPLETION_OK ||
      send_state.completion.bytes != fixture->payload_size ||
      send_state.completion.user_data != 2u ||
      send_state.completion.address_length != 0u)
    return SALTS_EIO;
  return memcmp(fixture->sent, fixture->received, fixture->payload_size) == 0
             ? SALTS_OK
             : SALTS_EIO;
}

static int udp_style_measure_backendstatic int udp_style_measure_backend(native_io_backend_kind kind, udp_style_kind style,
                                      size_t payload_size, udp_style_result *out) {
  udp_style_backend_fixture fixture;
  uint64_t *latencies =
      (uint64_t *)calloc(UDP_STYLE_MEASURED_TRANSFERS, sizeof(*latencies));
  int status;

  if (latencies == NULL) return SALTS_ENOMEM;
  status = udp_style_backend_fixture_init(&fixture, kind, payload_size);
  if (status != SALTS_OK) goto cleanup;

  for (size_t index = 0u; index < UDP_STYLE_WARMUP_TRANSFERS; ++index) {
    status = style == UDP_STYLE_DIRECT ? udp_style_direct_transfer(&fixture)
                                        : udp_style_coroutine_transfer(&fixture);
    if (status != SALTS_OK) goto cleanup;
  }

  memset(out, 0, sizeof(*out));
  out->style = udp_style_name(style);
  out->payload_size = payload_size;
  out->transfers = UDP_STYLE_MEASURED_TRANSFERS;
  for (size_t index = 0u; index < UDP_STYLE_MEASURED_TRANSFERS; ++index) {
    const uint64_t started = salts_hrtime();
    status = style == UDP_STYLE_DIRECT ? udp_style_direct_transfer(&fixture)
                                        : udp_style_coroutine_transfer(&fixture);
    latencies[index] = salts_hrtime() - started;
    if (status != SALTS_OK) goto cleanup;
    out->wall_ns += latencies[index];
  }
  status = udp_style_finalize_result(out, latencies);

cleanup:
  {
    const int destroy_status = udp_style_backend_fixture_destroy(&fixture);
    if (status == SALTS_OK) status = destroy_status;
  }
  free(latencies);
  return status;
}

static void udp_style_sharded_attach(native_io_sharded_context *context, void *arg) {
  udp_style_sharded_fixture *fixture = (udp_style_sharded_fixture *)arg;
  fixture->attach_status = native_io_sharded_context_attach_socket(
      context, (uintptr_t)fixture->sockets[0], &fixture->endpoints[0]);
  if (fixture->attach_status == SALTS_OK)
    fixture->attach_status = native_io_sharded_context_attach_socket(
      context, (uintptr_t)fixture->sockets[1], &fixture->endpoints[1]);
}

static void udp_style_sharded_release(native_io_sharded_context *context, void *arg) {
  udp_style_sharded_fixture *fixture = (udp_style_sharded_fixture *)arg;
  int status = SALTS_OK;
  for (size_t index = 0u; index < 2u; ++index) {
    if (!native_io_sharded_endpoint_valid(fixture->endpoints[index])) continue;
    {
      const int release_status =
          native_io_sharded_context_release_socket(context, fixture->endpoints[index]);
      if (status == SALTS_OK) status = release_status;
      fixture->endpoints[index] = (native_io_sharded_endpoint){0};
    }
  }
  fixture->release_status = status;
}

static int udp_style_sharded_fixture_init(udp_style_sharded_fixture *fixture,
                                           native_io_backend_kind kind,
                                           size_t payload_size) {
  const native_io_sharded_config config = {
      2u, UDP_STYLE_SHARDED_QUEUE_CAPACITY,
      {kind, 2u, UDP_STYLE_REQUEST_CAPACITY, UDP_STYLE_COMPLETION_CAPACITY}};
  native_io_sharded_task attach_task;
  int status;

  memset(fixture, 0, sizeof(*fixture));
  fixture->sockets[0] = UDP_STYLE_INVALID_SOCKET;
  fixture->sockets[1] = UDP_STYLE_INVALID_SOCKET;
  fixture->payload_size = payload_size;
  fixture->sent = (unsigned char *)malloc(payload_size);
  fixture->received = (unsigned char *)malloc(payload_size);
  if (fixture->sent == NULL || fixture->received == NULL) return SALTS_ENOMEM;
  memset(fixture->sent, 0x5au, payload_size);
  memset(fixture->received, 0, payload_size);

  status = udp_style_make_pair(fixture->sockets, fixture->addresses);
  if (status != SALTS_OK) return status;
  status = native_io_sharded_create(&config, &fixture->runtime);
  if (status != SALTS_OK) return status;

  attach_task =
      (native_io_sharded_task){udp_style_sharded_attach, NULL, NULL, fixture};
  status = native_io_sharded_submit_to(fixture->runtime, 1u, &attach_task);
  if (status == SALTS_OK) status = native_io_sharded_wait(fixture->runtime);
  if (status == SALTS_OK) status = fixture->attach_status;
  if (status == SALTS_OK &&
      (native_io_sharded_endpoint_owner_shard(fixture->endpoints[0]) != 1u ||
       native_io_sharded_endpoint_owner_shard(fixture->endpoints[1]) != 1u))
    status = SALTS_EPROTO;
  return status;
}

static int udp_style_sharded_fixture_destroy(udp_style_sharded_fixture *fixture) {
  int status = SALTS_OK;

  for (size_t index = 0u; index < 2u; ++index) {
    if (fixture->sockets[index] != UDP_STYLE_INVALID_SOCKET) {
      udp_style_close_socket(fixture->sockets[index]);
      fixture->sockets[index] = UDP_STYLE_INVALID_SOCKET;
    }
  }

  if (fixture->runtime != NULL) {
    native_io_sharded_task release_task = {
        udp_style_sharded_release, NULL, NULL, fixture};
    int release_submit =
        native_io_sharded_submit_to(fixture->runtime, 1u, &release_task);
    if (release_submit == SALTS_OK) release_submit = native_io_sharded_wait(fixture->runtime);
    if (release_submit == SALTS_OK) release_submit = fixture->release_status;
    if (status == SALTS_OK) status = release_submit;
    {
      const int destroy_status = native_io_sharded_destroy(fixture->runtime);
      if (status == SALTS_OK) status = destroy_status;
    }
    fixture->runtime = NULL;
  }

  free(fixture->received);
  free(fixture->sent);
  fixture->received = NULL;
  fixture->sent = NULL;
  return status;
}

static void udp_style_sharded_admission(native_io_sharded_context *context,
                                        int status,
                                        native_io_sharded_request request,
                                        void *arg) {
  udp_style_sharded_operation *state = (udp_style_sharded_operation *)arg;
  state->admission_status = status;
  state->request = request;
  state->admission_shard = native_io_sharded_context_shard(context);
}

static void udp_style_sharded_terminal(
    native_io_sharded_context *context,
    const native_io_sharded_completion *completion, void *arg) {
  udp_style_sharded_operation *state = (udp_style_sharded_operation *)arg;
  state->kind = completion->kind;
  state->terminal_status = completion->status;
  state->bytes = completion->bytes;
  state->user_data = completion->user_data;
  state->address_length = completion->address_length;
  state->terminal_shard = native_io_sharded_context_shard(context);
  ++state->terminal_count;
}

static void udp_style_sharded_finalize(void *arg) {
  udp_style_sharded_operation *state = (udp_style_sharded_operation *)arg;
  ++state->finalize_count;
}

static void udp_style_sharded_reset_operation(udp_style_sharded_operation *state) {
  memset(state, 0, sizeof(*state));
  state->admission_status = SALTS_EIO;
  state->terminal_status = SALTS_EIO;
  state->expected_owner_shard = SIZE_MAX;
  state->admission_shard = SIZE_MAX;
  state->terminal_shard = SIZE_MAX;
}

static int udp_style_sharded_submit_operation(
    udp_style_sharded_fixture *fixture, bool send,
    udp_style_sharded_operation *state) {
  native_io_sharded_operation operation = {0};
  const size_t endpoint_index = send ? 0u : 1u;
  udp_style_sharded_reset_operation(state);
  state->expected_owner_shard =
      native_io_sharded_endpoint_owner_shard(fixture->endpoints[endpoint_index]);

  operation.kind = send ? NATIVE_IO_OPERATION_UDP_SEND_TO
                        : NATIVE_IO_OPERATION_UDP_RECV_FROM;
  operation.endpoint = fixture->endpoints[endpoint_index];
  operation.buffer = send ? fixture->sent : fixture->received;
  operation.length = fixture->payload_size;
  operation.user_data = send ? 2u : 1u;
  if (send) {
    state->destination = fixture->addresses[1];
    operation.address = &state->destination;
    operation.address_capacity = sizeof(state->destination);
    operation.address_length = sizeof(state->destination);
  } else {
    memset(&state->peer_address, 0, sizeof(state->peer_address));
    operation.address = &state->peer_address;
    operation.address_capacity = sizeof(state->peer_address);
  }

  const native_io_sharded_ownership ownership = {
      udp_style_sharded_terminal, udp_style_sharded_finalize, state};
  return native_io_sharded_submit_owned(
      fixture->runtime, &operation, &ownership,
      udp_style_sharded_admission, state);
}

static int udp_style_sharded_validate_terminal(
    udp_style_sharded_fixture *fixture, bool send,
    udp_style_sharded_operation *state) {
  if (state->terminal_count == 0u) return SALTS_ETIMEDOUT;
  if (state->terminal_count != 1u || state->finalize_count != 1u)
    return SALTS_EPROTO;
  if (state->expected_owner_shard == SIZE_MAX ||
      state->admission_shard != state->expected_owner_shard ||
      state->terminal_shard != state->expected_owner_shard ||
      !native_io_sharded_request_valid(state->request) ||
      native_io_sharded_request_owner_shard(state->request) !=
          state->expected_owner_shard)
    return SALTS_EPROTO;
  if (state->kind != NATIVE_IO_COMPLETION_OK ||
      state->bytes != fixture->payload_size ||
      state->user_data != (send ? 2u : 1u))
    return state->terminal_status != SALTS_OK ? state->terminal_status
                                              : SALTS_EIO;
  if (send) return state->address_length == 0u ? SALTS_OK : SALTS_EPROTO;
  return udp_style_validate_peer(&state->peer_address, state->address_length,
                                 &fixture->addresses[0]);
}

static void udp_style_sharded_observe_task(native_io_sharded_context *context, void *arg) {
  udp_style_sharded_observe *observe = (udp_style_sharded_observe *)arg;
  observe->count = 0u;
  observe->status = native_io_sharded_context_observe(
      context, observe->events, UDP_STYLE_COMPLETION_CAPACITY,
      UDP_STYLE_TIMEOUT_MS, &observe->count);
}

static int udp_style_sharded_transfer(
    udp_style_sharded_fixture *fixture,
    native_io_sharded_context *same_owner_context) {
  udp_style_sharded_operation receive_state;
  udp_style_sharded_operation send_state;
  bool receive_done = false;
  bool send_done = false;
  int status;

  memset(fixture->received, 0, fixture->payload_size);
  status = udp_style_sharded_submit_operation(fixture, false, &receive_state);
  if (status != SALTS_OK) return status;
  status = udp_style_sharded_submit_operation(fixture, true, &send_state);
  if (status != SALTS_OK) return status;

  if (same_owner_context == NULL) {
    status = native_io_sharded_wait(fixture->runtime);
    if (status != SALTS_OK) return status;
  }

  if (receive_state.admission_status != SALTS_OK)
    return receive_state.admission_status;
  if (send_state.admission_status != SALTS_OK)
    return send_state.admission_status;

  while (!receive_done || !send_done) {
    udp_style_sharded_observe observe = {0};
    if (same_owner_context != NULL) {
      udp_style_sharded_observe_task(same_owner_context, &observe);
    } else {
      native_io_sharded_task observe_task = {
          udp_style_sharded_observe_task, NULL, NULL, &observe};
      status = native_io_sharded_submit_to(fixture->runtime, 1u, &observe_task);
      if (status == SALTS_OK) status = native_io_sharded_wait(fixture->runtime);
      if (status != SALTS_OK) return status;
    }

    if (observe.status != SALTS_OK) return observe.status;
    if (observe.count == 0u) return SALTS_EIO;

    if (!receive_done && receive_state.terminal_count != 0u) {
      status = udp_style_sharded_validate_terminal(
          fixture, false, &receive_state);
      if (status != SALTS_OK) return status;
      receive_done = true;
    }
    if (!send_done && send_state.terminal_count != 0u) {
      status = udp_style_sharded_validate_terminal(
          fixture, true, &send_state);
      if (status != SALTS_OK) return status;
      send_done = true;
    }
  }

  return memcmp(fixture->sent, fixture->received, fixture->payload_size) == 0
             ? SALTS_OK
             : SALTS_EIO;
}

static uint64_t udp_style_stats_deltastatic uint64_t udp_style_stats_delta(uint64_t after, uint64_t before) {
  return after >= before ? after - before : 0u;
}

static void udp_style_same_driver_run(native_io_sharded_context *context, void *arg) {
  udp_style_same_driver *driver = (udp_style_same_driver *)arg;
  udp_style_sharded_fixture *fixture = driver->fixture;

  driver->status = SALTS_OK;
  for (size_t index = 0u; index < UDP_STYLE_WARMUP_TRANSFERS; ++index) {
    driver->status = udp_style_sharded_transfer(fixture, context);
    if (driver->status != SALTS_OK) return;
  }

  if (!native_io_sharded_get_stats(fixture->runtime, &driver->before)) {
    driver->status = SALTS_EIO;
    return;
  }

  for (size_t index = 0u; index < UDP_STYLE_MEASURED_TRANSFERS; ++index) {
    const uint64_t started = salts_hrtime();
    driver->status = udp_style_sharded_transfer(fixture, context);
    driver->latencies[index] = salts_hrtime() - started;
    if (driver->status != SALTS_OK) return;
  }

  if (!native_io_sharded_get_stats(fixture->runtime, &driver->after))
    driver->status = SALTS_EIO;
}

static int udp_style_measure_sharded(native_io_backend_kind kind,
                                      udp_style_kind style,
                                      size_t payload_size,
                                      udp_style_result *out) {
  udp_style_sharded_fixture fixture;
  uint64_t *latencies =
      (uint64_t *)calloc(UDP_STYLE_MEASURED_TRANSFERS, sizeof(*latencies));
  native_io_sharded_stats before = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
  native_io_sharded_stats after = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
  int status;

  if (latencies == NULL) return SALTS_ENOMEM;
  status = udp_style_sharded_fixture_init(&fixture, kind, payload_size);
  if (status != SALTS_OK) goto cleanup;

  if (style == UDP_STYLE_SHARDED_SAME_OWNER) {
    udp_style_same_driver driver = {
        &fixture, latencies,
        NATIVE_IO_SHARDED_STATS_V1_INITIALIZER,
        NATIVE_IO_SHARDED_STATS_V1_INITIALIZER,
        SALTS_OK};
    native_io_sharded_task task = {
        udp_style_same_driver_run, NULL, NULL, &driver};
    status = native_io_sharded_submit_to(fixture.runtime, 1u, &task);
    if (status == SALTS_OK) status = native_io_sharded_wait(fixture.runtime);
    if (status == SALTS_OK) status = driver.status;
    before = driver.before;
    after = driver.after;
  } else {
    for (size_t index = 0u; index < UDP_STYLE_WARMUP_TRANSFERS; ++index) {
      status = udp_style_sharded_transfer(&fixture, NULL);
      if (status != SALTS_OK) goto cleanup;
    }
    if (!native_io_sharded_get_stats(fixture.runtime, &before)) {
      status = SALTS_EIO;
      goto cleanup;
    }
    for (size_t index = 0u; index < UDP_STYLE_MEASURED_TRANSFERS; ++index) {
      const uint64_t started = salts_hrtime();
      status = udp_style_sharded_transfer(&fixture, NULL);
      latencies[index] = salts_hrtime() - started;
      if (status != SALTS_OK) goto cleanup;
    }
    if (!native_io_sharded_get_stats(fixture.runtime, &after)) {
      status = SALTS_EIO;
      goto cleanup;
    }
  }

  if (status == SALTS_OK) {
    memset(out, 0, sizeof(*out));
    out->style = udp_style_name(style);
    out->payload_size = payload_size;
    out->transfers = UDP_STYLE_MEASURED_TRANSFERS;
    for (size_t index = 0u; index < UDP_STYLE_MEASURED_TRANSFERS; ++index)
      out->wall_ns += latencies[index];
    out->queued_dispatches =
        udp_style_stats_delta(after.queued_dispatches, before.queued_dispatches);
    out->rejected_tasks =
        udp_style_stats_delta(after.rejected_tasks, before.rejected_tasks);
    {
      const uint64_t direct_tasks =
          udp_style_stats_delta(after.same_shard_direct_tasks,
                                 before.same_shard_direct_tasks);
      out->same_owner_direct_tasks_per_transfer =
          (double)direct_tasks / (double)UDP_STYLE_MEASURED_TRANSFERS;
    }
    out->message_hops_per_transfer =
        (double)out->queued_dispatches /
        (double)UDP_STYLE_MEASURED_TRANSFERS;
    out->peak_command_slots = after.peak_command_slots;
    status = udp_style_finalize_result(out, latencies);
  }

cleanup:
  {
    const int destroy_status = udp_style_sharded_fixture_destroy(&fixture);
    if (status == SALTS_OK) status = destroy_status;
  }
  free(latencies);
  return status;
}

static FILE *udp_style_open_csv(void) {
  const char *prefix = getenv("NATIVE_IO_UDP_STYLE_OUTPUT");
  char path[1024];
  if (prefix == NULL || *prefix == '\0') return NULL;
  if (snprintf(path, sizeof(path), "%s.csv", prefix) < 0) return NULL;
  return fopen(path, "w");
}

static void udp_style_print_csv_row(FILE *stream, const char *backend,
                                     const udp_style_result *result) {
  fprintf(stream,
          "%s,%s,%zu,%zu,%.6f,%.6f,%.6f,%.6f,%.6f,%" PRIu64
          ",%" PRIu64 ",%" PRIu64 "\n",
          backend, result->style, result->payload_size, result->transfers,
          (double)result->p50_ns / 1000.0,
          (double)result->p95_ns / 1000.0,
          result->mib_per_second,
          result->message_hops_per_transfer,
          result->same_owner_direct_tasks_per_transfer,
          result->queued_dispatches,
          result->rejected_tasks,
          result->peak_command_slots);
}

int main(void) {
  const native_io_backend_kind kind = udp_style_backend();
  const char *backend = udp_style_backend_name(kind);
  const udp_style_kind styles[] = {
      UDP_STYLE_DIRECT,
      UDP_STYLE_COROUTINE,
      UDP_STYLE_SHARDED_SAME_OWNER,
      UDP_STYLE_SHARDED_CROSS_OWNER};
  const size_t style_count = sizeof(styles) / sizeof(styles[0]);
  const size_t payload_count =
      sizeof(UDP_STYLE_PAYLOADS) / sizeof(UDP_STYLE_PAYLOADS[0]);
  udp_style_result results[4][3];
  FILE *csv = NULL;

  if (kind == (native_io_backend_kind)0 ||
      !native_io_backend_kind_supported(kind)) {
    fprintf(stderr, "unsupported NativeIO UDP DATAGRAM style backend: %s\n", backend);
    return 2;
  }

  {
    const int network_status = udp_style_network_start();
    if (network_status != SALTS_OK) {
      fprintf(stderr, "UDP network initialization failed: %d\n", network_status);
      return 1;
    }
  }

  memset(results, 0, sizeof(results));
  for (size_t style_index = 0u; style_index < style_count; ++style_index) {
    for (size_t payload_index = 0u; payload_index < payload_count; ++payload_index) {
      const udp_style_kind style = styles[style_index];
      int status;
      if (style == UDP_STYLE_DIRECT || style == UDP_STYLE_COROUTINE)
        status = udp_style_measure_backend(kind, style,
                                            UDP_STYLE_PAYLOADS[payload_index],
                                            &results[style_index][payload_index]);
      else
        status = udp_style_measure_sharded(kind, style,
                                            UDP_STYLE_PAYLOADS[payload_index],
                                            &results[style_index][payload_index]);
      if (status != SALTS_OK) {
        fprintf(stderr, "%s %s %zu-byte benchmark failed: %d\n",
                backend, udp_style_name(style),
                UDP_STYLE_PAYLOADS[payload_index], status);
        udp_style_network_stop();
        return 1;
      }
    }
  }

  printf("# NativeIO UDP DATAGRAM execution-style benchmark\n\n");
  printf("Backend: %s\n\n", backend);
  printf("Each cell measures %d complete one-way transfers after %d warmups. "
         "Latency percentiles are individual complete-transfer latencies.\n\n",
         UDP_STYLE_MEASURED_TRANSFERS, UDP_STYLE_WARMUP_TRANSFERS);
  printf("| style | payload | p50 us | p95 us | MiB/s | message hops/transfer | "
         "same-owner direct tasks/transfer | queued dispatches | rejected | peak command slots |\n");
  printf("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t style_index = 0u; style_index < style_count; ++style_index) {
    for (size_t payload_index = 0u; payload_index < payload_count; ++payload_index) {
      const udp_style_result *result = &results[style_index][payload_index];
      printf("| %s | %zu | %.3f | %.3f | %.2f | %.3f | %.3f | %" PRIu64
             " | %" PRIu64 " | %" PRIu64 " |\n",
             result->style, result->payload_size,
             (double)result->p50_ns / 1000.0,
             (double)result->p95_ns / 1000.0,
             result->mib_per_second,
             result->message_hops_per_transfer,
             result->same_owner_direct_tasks_per_transfer,
             result->queued_dispatches,
             result->rejected_tasks,
             result->peak_command_slots);
    }
  }

  csv = udp_style_open_csv();
  if (csv != NULL) {
    fprintf(csv,
            "backend,style,payload_bytes,transfers,p50_us,p95_us,mib_per_second,"
            "message_hops_per_transfer,same_owner_direct_tasks_per_transfer,"
            "queued_dispatches,rejected_tasks,peak_command_slots\n");
    for (size_t style_index = 0u; style_index < style_count; ++style_index)
      for (size_t payload_index = 0u; payload_index < payload_count; ++payload_index)
        udp_style_print_csv_row(csv, backend,
                                 &results[style_index][payload_index]);
    fclose(csv);
  }

  udp_style_network_stop();
  return 0;
}
