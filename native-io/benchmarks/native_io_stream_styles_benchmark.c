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
typedef SOCKET stream_style_socket;
typedef int stream_style_socklen;
  #define STREAM_STYLE_INVALID_SOCKET INVALID_SOCKET
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <sys/socket.h>
  #include <unistd.h>
typedef int stream_style_socket;
typedef socklen_t stream_style_socklen;
  #define STREAM_STYLE_INVALID_SOCKET (-1)
#endif

enum {
  STREAM_STYLE_WARMUP_TRANSFERS = 32,
  STREAM_STYLE_MEASURED_TRANSFERS = 512,
  STREAM_STYLE_TIMEOUT_MS = 5000,
  STREAM_STYLE_REQUEST_CAPACITY = 4,
  STREAM_STYLE_COMPLETION_CAPACITY = 4,
  STREAM_STYLE_SHARDED_QUEUE_CAPACITY = 64
};

static const size_t STREAM_STYLE_PAYLOADS[] = {1024u, 8192u, 32768u, 65536u};

typedef enum stream_style_kind {
  STREAM_STYLE_DIRECT = 0,
  STREAM_STYLE_COROUTINE,
  STREAM_STYLE_SHARDED_SAME_OWNER,
  STREAM_STYLE_SHARDED_CROSS_OWNER
} stream_style_kind;

typedef struct stream_style_result {
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
  uint64_t observe_calls;
  uint64_t completion_count;
  uint64_t completion_batches;
  uint64_t max_completion_batch;
} stream_style_result;

typedef struct stream_style_backend_fixture {
  native_io_backend backend;
  native_io_endpoint endpoints[2];
  stream_style_socket sockets[2];
  unsigned char *sent;
  unsigned char *received;
  size_t payload_size;
  uint64_t observe_calls;
  uint64_t completion_count;
  uint64_t completion_batches;
  uint64_t max_completion_batch;
} stream_style_backend_fixture;

typedef struct stream_style_coroutine_operation {
  stream_style_backend_fixture *fixture;
  unsigned char *buffer;
  size_t length;
  size_t offset;
  int status;
  bool write;
  bool vector_write;
  bool done;
} stream_style_coroutine_operation;

typedef struct stream_style_sharded_fixture {
  native_io_sharded *runtime;
  native_io_sharded_endpoint endpoints[2];
  stream_style_socket sockets[2];
  unsigned char *sent;
  unsigned char *received;
  size_t payload_size;
  int attach_status;
  int release_status;
  uint64_t observe_calls;
  uint64_t completion_count;
  uint64_t completion_batches;
  uint64_t max_completion_batch;
} stream_style_sharded_fixture;

typedef struct stream_style_sharded_operation {
  int admission_status;
  native_io_sharded_request request;
  native_io_completion_kind kind;
  int terminal_status;
  size_t bytes;
  uintptr_t user_data;
  unsigned terminal_count;
  unsigned finalize_count;
} stream_style_sharded_operation;

typedef struct stream_style_sharded_observe {
  stream_style_sharded_fixture *fixture;
  native_io_sharded_completion events[STREAM_STYLE_COMPLETION_CAPACITY];
  size_t count;
  int status;
} stream_style_sharded_observe;

typedef struct stream_style_same_driver {
  stream_style_sharded_fixture *fixture;
  uint64_t *latencies;
  native_io_sharded_stats before;
  native_io_sharded_stats after;
  bool trace;
  int status;
} stream_style_same_driver;

static native_io_backend_kind stream_style_backend(void) {
  const char *value = getenv("NATIVE_IO_STREAM_STYLE_BACKEND");
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

static const char *stream_style_backend_name(native_io_backend_kind kind) {
  switch (kind) {
    case NATIVE_IO_BACKEND_EPOLL: return "epoll";
    case NATIVE_IO_BACKEND_IO_URING: return "io_uring";
    case NATIVE_IO_BACKEND_IOCP: return "iocp";
    case NATIVE_IO_BACKEND_KQUEUE: return "kqueue";
    default: return "unsupported";
  }
}

static const char *stream_style_name(stream_style_kind style) {
  switch (style) {
    case STREAM_STYLE_DIRECT: return "direct";
    case STREAM_STYLE_COROUTINE: return "coroutine";
    case STREAM_STYLE_SHARDED_SAME_OWNER: return "sharded_same_owner";
    case STREAM_STYLE_SHARDED_CROSS_OWNER: return "sharded_cross_owner";
    default: return "unknown";
  }
}

typedef struct stream_style_trace_selection {
  bool enabled;
  stream_style_kind style;
  size_t payload_size;
} stream_style_trace_selection;

static int stream_style_parse_trace(stream_style_trace_selection *selection) {
  const char *value = getenv("NATIVE_IO_STREAM_STYLE_TRACE");
  const char *separator;
  char style_name[64];
  char *end = NULL;
  unsigned long long payload;
  size_t style_length;

  if (selection == NULL) return SALTS_EINVAL;
  *selection = (stream_style_trace_selection){0};
  if (value == NULL || *value == '\0') return SALTS_OK;
  separator = strchr(value, ':');
  if (separator == NULL || separator == value || separator[1] == '\0') return SALTS_EINVAL;
  style_length = (size_t)(separator - value);
  if (style_length >= sizeof(style_name)) return SALTS_ERANGE;
  memcpy(style_name, value, style_length);
  style_name[style_length] = '\0';

  if (strcmp(style_name, "direct") == 0)
    selection->style = STREAM_STYLE_DIRECT;
  else if (strcmp(style_name, "coroutine") == 0)
    selection->style = STREAM_STYLE_COROUTINE;
  else if (strcmp(style_name, "sharded_same_owner") == 0)
    selection->style = STREAM_STYLE_SHARDED_SAME_OWNER;
  else if (strcmp(style_name, "sharded_cross_owner") == 0)
    selection->style = STREAM_STYLE_SHARDED_CROSS_OWNER;
  else
    return SALTS_EINVAL;

  payload = strtoull(separator + 1, &end, 10);
  if (end == separator + 1 || *end != '\0' || payload == 0u ||
      payload > (unsigned long long)SIZE_MAX)
    return SALTS_EINVAL;
  selection->payload_size = (size_t)payload;
  selection->enabled = true;
  return SALTS_OK;
}

static void stream_style_trace_marker(bool begin, stream_style_kind style,
                                      size_t payload_size) {
  fprintf(stderr,
          "NATIVE_IO_STYLE_MEASURE_%s style=%s payload=%zu transfers=%d\n",
          begin ? "BEGIN" : "END", stream_style_name(style), payload_size,
          STREAM_STYLE_MEASURED_TRANSFERS);
  fflush(stderr);
}

static void stream_style_reset_backend_completion_stats(
    stream_style_backend_fixture *fixture) {
  fixture->observe_calls = 0u;
  fixture->completion_count = 0u;
  fixture->completion_batches = 0u;
  fixture->max_completion_batch = 0u;
}

static void stream_style_reset_sharded_completion_stats(
    stream_style_sharded_fixture *fixture) {
  fixture->observe_calls = 0u;
  fixture->completion_count = 0u;
  fixture->completion_batches = 0u;
  fixture->max_completion_batch = 0u;
}

static void stream_style_record_batch(uint64_t *observe_calls,
                                      uint64_t *completion_count,
                                      uint64_t *completion_batches,
                                      uint64_t *max_completion_batch,
                                      uint64_t count) {
  ++*observe_calls;
  *completion_count += count;
  if (count != 0u) {
    ++*completion_batches;
    if (count > *max_completion_batch) *max_completion_batch = count;
  }
}


static int stream_style_socket_error(void) {
#if defined(_WIN32)
  const int error = WSAGetLastError();
#else
  const int error = errno;
#endif
  return error == 0 ? SALTS_EIO : -error;
}

static int stream_style_network_start(void) {
#if defined(_WIN32)
  WSADATA data;
  const int status = WSAStartup(MAKEWORD(2, 2), &data);
  return status == 0 ? SALTS_OK : -status;
#else
  return SALTS_OK;
#endif
}

static void stream_style_network_stop(void) {
#if defined(_WIN32)
  (void)WSACleanup();
#endif
}

static void stream_style_close_socket(stream_style_socket socket_value) {
  if (socket_value == STREAM_STYLE_INVALID_SOCKET) return;
#if defined(_WIN32)
  (void)closesocket(socket_value);
#else
  (void)close(socket_value);
#endif
}

static int stream_style_set_nonblocking(stream_style_socket socket_value) {
#if defined(_WIN32)
  u_long enabled = 1u;
  return ioctlsocket(socket_value, FIONBIO, &enabled) == 0
             ? SALTS_OK
             : stream_style_socket_error();
#else
  const int flags = fcntl(socket_value, F_GETFL, 0);
  if (flags < 0) return stream_style_socket_error();
  if ((flags & O_NONBLOCK) != 0) return SALTS_OK;
  return fcntl(socket_value, F_SETFL, flags | O_NONBLOCK) == 0
             ? SALTS_OK
             : stream_style_socket_error();
#endif
}

static int stream_style_set_nodelay(stream_style_socket socket_value) {
  const int enabled = 1;
#if defined(_WIN32)
  return setsockopt(socket_value, IPPROTO_TCP, TCP_NODELAY,
                    (const char *)&enabled, (int)sizeof(enabled)) == 0
             ? SALTS_OK
             : stream_style_socket_error();
#else
  return setsockopt(socket_value, IPPROTO_TCP, TCP_NODELAY,
                    &enabled, (socklen_t)sizeof(enabled)) == 0
             ? SALTS_OK
             : stream_style_socket_error();
#endif
}

static int stream_style_disable_sigpipe(stream_style_socket socket_value) {
#if defined(SO_NOSIGPIPE)
  const int enabled = 1;
  return setsockopt(socket_value, SOL_SOCKET, SO_NOSIGPIPE,
                    &enabled, (socklen_t)sizeof(enabled)) == 0
             ? SALTS_OK
             : stream_style_socket_error();
#else
  (void)socket_value;
  return SALTS_OK;
#endif
}

static int stream_style_bind_loopback(stream_style_socket socket_value,
                                      struct sockaddr_in *address) {
  stream_style_socklen address_length = (stream_style_socklen)sizeof(*address);
  memset(address, 0, sizeof(*address));
  address->sin_family = AF_INET;
  address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address->sin_port = 0u;
  if (bind(socket_value, (const struct sockaddr *)address,
           (stream_style_socklen)sizeof(*address)) != 0)
    return stream_style_socket_error();
  if (getsockname(socket_value, (struct sockaddr *)address, &address_length) != 0)
    return stream_style_socket_error();
  return SALTS_OK;
}

static int stream_style_make_pair(stream_style_socket sockets[2]) {
  stream_style_socket listener = STREAM_STYLE_INVALID_SOCKET;
  struct sockaddr_in address;
  int status;

  sockets[0] = STREAM_STYLE_INVALID_SOCKET;
  sockets[1] = STREAM_STYLE_INVALID_SOCKET;
  listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == STREAM_STYLE_INVALID_SOCKET) return stream_style_socket_error();

  status = stream_style_bind_loopback(listener, &address);
  if (status == SALTS_OK && listen(listener, 1) != 0)
    status = stream_style_socket_error();
  if (status == SALTS_OK) {
    sockets[0] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockets[0] == STREAM_STYLE_INVALID_SOCKET)
      status = stream_style_socket_error();
  }
  if (status == SALTS_OK &&
      connect(sockets[0], (const struct sockaddr *)&address,
              (stream_style_socklen)sizeof(address)) != 0)
    status = stream_style_socket_error();
  if (status == SALTS_OK) {
    sockets[1] = accept(listener, NULL, NULL);
    if (sockets[1] == STREAM_STYLE_INVALID_SOCKET)
      status = stream_style_socket_error();
  }

  stream_style_close_socket(listener);
  listener = STREAM_STYLE_INVALID_SOCKET;

  for (size_t index = 0u; status == SALTS_OK && index < 2u; ++index) {
    status = stream_style_set_nodelay(sockets[index]);
    if (status == SALTS_OK) status = stream_style_disable_sigpipe(sockets[index]);
    if (status == SALTS_OK) status = stream_style_set_nonblocking(sockets[index]);
  }

  if (status != SALTS_OK) {
    stream_style_close_socket(sockets[0]);
    stream_style_close_socket(sockets[1]);
    sockets[0] = STREAM_STYLE_INVALID_SOCKET;
    sockets[1] = STREAM_STYLE_INVALID_SOCKET;
  }
  return status;
}

static int stream_style_compare_u64(const void *left, const void *right) {
  const uint64_t a = *(const uint64_t *)left;
  const uint64_t b = *(const uint64_t *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static int stream_style_finalize_result(stream_style_result *result, uint64_t *latencies) {
  if (result == NULL || latencies == NULL || result->transfers == 0u) return SALTS_EINVAL;
  qsort(latencies, result->transfers, sizeof(*latencies), stream_style_compare_u64);
  result->p50_ns = latencies[(result->transfers - 1u) * 50u / 100u];
  result->p95_ns = latencies[(result->transfers - 1u) * 95u / 100u];
  result->mib_per_second =
      result->wall_ns == 0u
          ? 0.0
          : (double)result->payload_size * (double)result->transfers * 1.0e9 /
                (double)result->wall_ns / (1024.0 * 1024.0);
  return SALTS_OK;
}

static int stream_style_backend_fixture_init(stream_style_backend_fixture *fixture,
                                           native_io_backend_kind kind,
                                           size_t payload_size) {
  const native_io_backend_config config = {
      kind, 2u, STREAM_STYLE_REQUEST_CAPACITY, STREAM_STYLE_COMPLETION_CAPACITY};
  int status;

  memset(fixture, 0, sizeof(*fixture));
  fixture->sockets[0] = STREAM_STYLE_INVALID_SOCKET;
  fixture->sockets[1] = STREAM_STYLE_INVALID_SOCKET;
  fixture->payload_size = payload_size;
  fixture->sent = (unsigned char *)malloc(payload_size);
  fixture->received = (unsigned char *)malloc(payload_size);
  if (fixture->sent == NULL || fixture->received == NULL) return SALTS_ENOMEM;
  memset(fixture->sent, 0x5au, payload_size);
  memset(fixture->received, 0, payload_size);

  status = stream_style_make_pair(fixture->sockets);
  if (status != SALTS_OK) return status;
  status = native_io_backend_init(&fixture->backend, &config);
  if (status == SALTS_OK)
    status = native_io_backend_attach_socket(&fixture->backend, (uintptr_t)fixture->sockets[0],
                                           &fixture->endpoints[0]);
  if (status == SALTS_OK)
    status = native_io_backend_attach_socket(&fixture->backend, (uintptr_t)fixture->sockets[1],
                                           &fixture->endpoints[1]);
  return status;
}

static int stream_style_backend_fixture_destroy(stream_style_backend_fixture *fixture) {
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
    if (fixture->sockets[index] != STREAM_STYLE_INVALID_SOCKET) {
      stream_style_close_socket(fixture->sockets[index]);
      fixture->sockets[index] = STREAM_STYLE_INVALID_SOCKET;
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

static int stream_style_direct_transfer(stream_style_backend_fixture *fixture,
                                        bool vector_write) {
  size_t sent_offset = 0u;
  size_t received_offset = 0u;
  bool read_pending = false;
  bool write_pending = false;

  memset(fixture->received, 0, fixture->payload_size);
  while (sent_offset < fixture->payload_size || received_offset < fixture->payload_size) {
    native_io_completion events[STREAM_STYLE_COMPLETION_CAPACITY];
    size_t count = 0u;
    int status;

    if (!read_pending && received_offset < fixture->payload_size) {
      const native_io_operation operation = {
          .kind = NATIVE_IO_OPERATION_STREAM_RECV,
          .endpoint = fixture->endpoints[0],
          .buffer = fixture->received + received_offset,
          .length = fixture->payload_size - received_offset,
          .user_data = 1u};
      native_io_request request = {0};
      status = native_io_backend_submit(&fixture->backend, &operation, &request);
      if (status != SALTS_OK) return status;
      read_pending = true;
    }

    if (!write_pending && sent_offset < fixture->payload_size) {
      const size_t remaining = fixture->payload_size - sent_offset;
      native_io_request request = {0};
      if (vector_write) {
        const size_t first_length = remaining > 1u ? remaining / 2u : remaining;
        native_io_buffer_span spans[2] = {
            {fixture->sent + sent_offset, first_length},
            {fixture->sent + sent_offset + first_length, remaining - first_length}};
        const size_t span_count = spans[1].length == 0u ? 1u : 2u;
        const native_io_vector_operation operation = {
            NATIVE_IO_OPERATION_STREAM_SEND, fixture->endpoints[1], spans, span_count, 2u};
        status = native_io_backend_submit_vector(&fixture->backend, &operation, &request);
      } else {
        const native_io_operation operation = {
            .kind = NATIVE_IO_OPERATION_STREAM_SEND,
            .endpoint = fixture->endpoints[1],
            .buffer = fixture->sent + sent_offset,
            .length = remaining,
            .user_data = 2u};
        status = native_io_backend_submit(&fixture->backend, &operation, &request);
      }
      if (status != SALTS_OK) return status;
      write_pending = true;
    }

    status = native_io_backend_observe(&fixture->backend, events,
                                       STREAM_STYLE_COMPLETION_CAPACITY,
                                       STREAM_STYLE_TIMEOUT_MS, &count);
    if (status != SALTS_OK) return status;
    stream_style_record_batch(
        &fixture->observe_calls, &fixture->completion_count,
        &fixture->completion_batches, &fixture->max_completion_batch, count);
    if (count == 0u) return SALTS_EIO;

    for (size_t index = 0u; index < count; ++index) {
      const native_io_completion *event = &events[index];
      if (event->kind != NATIVE_IO_COMPLETION_OK || event->bytes == 0u)
        return event->status != SALTS_OK ? event->status : SALTS_EIO;
      if (event->user_data == 1u) {
        if (!read_pending || event->bytes > fixture->payload_size - received_offset)
          return SALTS_EPROTO;
        received_offset += event->bytes;
        read_pending = false;
      } else if (event->user_data == 2u) {
        if (!write_pending || event->bytes > fixture->payload_size - sent_offset)
          return SALTS_EPROTO;
        sent_offset += event->bytes;
        write_pending = false;
      } else {
        return SALTS_EPROTO;
      }
    }
  }

  return memcmp(fixture->sent, fixture->received, fixture->payload_size) == 0
             ? SALTS_OK
             : SALTS_EIO;
}

static void stream_style_coroutine_entry(native_io_coroutine *coroutine, void *arg) {
  stream_style_coroutine_operation *state = (stream_style_coroutine_operation *)arg;
  state->status = SALTS_OK;
  while (state->offset < state->length) {
    native_io_completion completion = {0};
    if (state->write && state->vector_write) {
      const size_t remaining = state->length - state->offset;
      const size_t first_length = remaining > 1u ? remaining / 2u : remaining;
      native_io_buffer_span spans[2] = {
          {state->buffer + state->offset, first_length},
          {state->buffer + state->offset + first_length, remaining - first_length}};
      const size_t span_count = spans[1].length == 0u ? 1u : 2u;
      const native_io_vector_operation operation = {
          NATIVE_IO_OPERATION_STREAM_SEND, state->fixture->endpoints[1],
          spans, span_count, 2u};
      state->status =
          native_io_coroutine_await_vector(coroutine, &operation, &completion);
    } else {
      const native_io_operation operation = {
          .kind = state->write ? NATIVE_IO_OPERATION_STREAM_SEND
                               : NATIVE_IO_OPERATION_STREAM_RECV,
          .endpoint = state->fixture->endpoints[state->write ? 1u : 0u],
          .buffer = state->buffer + state->offset,
          .length = state->length - state->offset,
          .user_data = state->write ? 2u : 1u};
      state->status =
          native_io_coroutine_await(coroutine, &operation, &completion);
    }
    if (state->status != SALTS_OK) break;
    if (completion.kind != NATIVE_IO_COMPLETION_OK || completion.bytes == 0u ||
        completion.bytes > state->length - state->offset) {
      state->status =
          completion.status != SALTS_OK ? completion.status : SALTS_EIO;
      break;
    }
    state->offset += completion.bytes;
  }
  state->done = true;
}

static int stream_style_coroutine_cancel_and_drain(
    stream_style_backend_fixture *fixture, native_io_coroutine_task task,
    stream_style_coroutine_operation *state) {
  native_io_completion events[STREAM_STYLE_COMPLETION_CAPACITY];
  int status;

  if (state->done || !native_io_coroutine_task_valid(task)) return SALTS_OK;
  status = native_io_backend_cancel_coroutine(&fixture->backend, task);
  if (status != SALTS_OK && status != SALTS_EALREADY) return status;
  while (!state->done) {
    size_t count = 0u;
    status = native_io_backend_observe(&fixture->backend, events,
                                       STREAM_STYLE_COMPLETION_CAPACITY,
                                       STREAM_STYLE_TIMEOUT_MS, &count);
    if (status != SALTS_OK) return status;
    if (count != 0u) return SALTS_EPROTO;
  }
  return SALTS_OK;
}

static int stream_style_coroutine_transfer(stream_style_backend_fixture *fixture,
                                           bool vector_write) {
  stream_style_coroutine_operation read_state = {
      fixture, fixture->received, fixture->payload_size, 0u, SALTS_OK, false, false, false};
  stream_style_coroutine_operation write_state = {
      fixture, fixture->sent, fixture->payload_size, 0u, SALTS_OK, true, vector_write, false};
  native_io_coroutine_task read_task = {0};
  native_io_coroutine_task write_task = {0};
  native_io_completion events[STREAM_STYLE_COMPLETION_CAPACITY];
  int status;

  memset(fixture->received, 0, fixture->payload_size);
  status = native_io_backend_spawn_coroutine(&fixture->backend,
                                             stream_style_coroutine_entry,
                                             &read_state, &read_task);
  if (status == SALTS_OK)
    status = native_io_backend_spawn_coroutine(&fixture->backend,
                                               stream_style_coroutine_entry,
                                               &write_state, &write_task);

  while (status == SALTS_OK && (!read_state.done || !write_state.done)) {
    const size_t read_before = read_state.offset;
    const size_t write_before = write_state.offset;
    size_t count = 0u;
    uint64_t completed = 0u;
    status = native_io_backend_observe(&fixture->backend, events,
                                       STREAM_STYLE_COMPLETION_CAPACITY,
                                       STREAM_STYLE_TIMEOUT_MS, &count);
    if (status == SALTS_OK && count != 0u) status = SALTS_EPROTO;
    if (status == SALTS_OK) {
      if (read_state.offset != read_before) ++completed;
      if (write_state.offset != write_before) ++completed;
      stream_style_record_batch(
          &fixture->observe_calls, &fixture->completion_count,
          &fixture->completion_batches, &fixture->max_completion_batch,
          completed);
    }
  }

  if (status != SALTS_OK) {
    const int failure = status;
    const int write_drain =
        stream_style_coroutine_cancel_and_drain(fixture, write_task, &write_state);
    const int read_drain =
        stream_style_coroutine_cancel_and_drain(fixture, read_task, &read_state);
    if (write_drain != SALTS_OK) return write_drain;
    if (read_drain != SALTS_OK) return read_drain;
    return failure;
  }
  if (read_state.status != SALTS_OK) return read_state.status;
  if (write_state.status != SALTS_OK) return write_state.status;
  return memcmp(fixture->sent, fixture->received, fixture->payload_size) == 0
             ? SALTS_OK
             : SALTS_EIO;
}

static int stream_style_measure_backend(native_io_backend_kind kind, stream_style_kind style,
                                      size_t payload_size, stream_style_result *out,
                                      bool trace, bool vector_write) {
  stream_style_backend_fixture fixture;
  uint64_t *latencies =
      (uint64_t *)calloc(STREAM_STYLE_MEASURED_TRANSFERS, sizeof(*latencies));
  int status;

  if (latencies == NULL) return SALTS_ENOMEM;
  status = stream_style_backend_fixture_init(&fixture, kind, payload_size);
  if (status != SALTS_OK) goto cleanup;

  for (size_t index = 0u; index < STREAM_STYLE_WARMUP_TRANSFERS; ++index) {
    status = style == STREAM_STYLE_DIRECT
                 ? stream_style_direct_transfer(&fixture, vector_write)
                 : stream_style_coroutine_transfer(&fixture, vector_write);
    if (status != SALTS_OK) goto cleanup;
  }

  stream_style_reset_backend_completion_stats(&fixture);
  if (trace) stream_style_trace_marker(true, style, payload_size);

  memset(out, 0, sizeof(*out));
  out->style = stream_style_name(style);
  out->payload_size = payload_size;
  out->transfers = STREAM_STYLE_MEASURED_TRANSFERS;
  for (size_t index = 0u; index < STREAM_STYLE_MEASURED_TRANSFERS; ++index) {
    const uint64_t started = salts_hrtime();
    status = style == STREAM_STYLE_DIRECT
                 ? stream_style_direct_transfer(&fixture, vector_write)
                 : stream_style_coroutine_transfer(&fixture, vector_write);
    latencies[index] = salts_hrtime() - started;
    if (status != SALTS_OK) goto cleanup;
    out->wall_ns += latencies[index];
  }
  if (trace) stream_style_trace_marker(false, style, payload_size);
  out->observe_calls = fixture.observe_calls;
  out->completion_count = fixture.completion_count;
  out->completion_batches = fixture.completion_batches;
  out->max_completion_batch = fixture.max_completion_batch;
  status = stream_style_finalize_result(out, latencies);

cleanup:
  {
    const int destroy_status = stream_style_backend_fixture_destroy(&fixture);
    if (status == SALTS_OK) status = destroy_status;
  }
  free(latencies);
  return status;
}

static void stream_style_sharded_attach(native_io_sharded_context *context, void *arg) {
  stream_style_sharded_fixture *fixture = (stream_style_sharded_fixture *)arg;
  fixture->attach_status = native_io_sharded_context_attach_socket(
      context, (uintptr_t)fixture->sockets[0], &fixture->endpoints[0]);
  if (fixture->attach_status == SALTS_OK)
    fixture->attach_status = native_io_sharded_context_attach_socket(
      context, (uintptr_t)fixture->sockets[1], &fixture->endpoints[1]);
}

static void stream_style_sharded_release(native_io_sharded_context *context, void *arg) {
  stream_style_sharded_fixture *fixture = (stream_style_sharded_fixture *)arg;
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

static int stream_style_sharded_fixture_init(stream_style_sharded_fixture *fixture,
                                           native_io_backend_kind kind,
                                           size_t payload_size) {
  const native_io_sharded_config config = {
      2u, STREAM_STYLE_SHARDED_QUEUE_CAPACITY,
      {kind, 2u, STREAM_STYLE_REQUEST_CAPACITY, STREAM_STYLE_COMPLETION_CAPACITY}};
  native_io_sharded_task attach_task;
  int status;

  memset(fixture, 0, sizeof(*fixture));
  fixture->sockets[0] = STREAM_STYLE_INVALID_SOCKET;
  fixture->sockets[1] = STREAM_STYLE_INVALID_SOCKET;
  fixture->payload_size = payload_size;
  fixture->sent = (unsigned char *)malloc(payload_size);
  fixture->received = (unsigned char *)malloc(payload_size);
  if (fixture->sent == NULL || fixture->received == NULL) return SALTS_ENOMEM;
  memset(fixture->sent, 0x5au, payload_size);
  memset(fixture->received, 0, payload_size);

  status = stream_style_make_pair(fixture->sockets);
  if (status != SALTS_OK) return status;
  status = native_io_sharded_create(&config, &fixture->runtime);
  if (status != SALTS_OK) return status;

  attach_task =
      (native_io_sharded_task){stream_style_sharded_attach, NULL, NULL, fixture};
  status = native_io_sharded_submit_to(fixture->runtime, 1u, &attach_task);
  if (status == SALTS_OK) status = native_io_sharded_wait(fixture->runtime);
  if (status == SALTS_OK) status = fixture->attach_status;
  return status;
}

static int stream_style_sharded_fixture_destroy(stream_style_sharded_fixture *fixture) {
  int status = SALTS_OK;

  for (size_t index = 0u; index < 2u; ++index) {
    if (fixture->sockets[index] != STREAM_STYLE_INVALID_SOCKET) {
      stream_style_close_socket(fixture->sockets[index]);
      fixture->sockets[index] = STREAM_STYLE_INVALID_SOCKET;
    }
  }

  if (fixture->runtime != NULL) {
    native_io_sharded_task release_task = {
        stream_style_sharded_release, NULL, NULL, fixture};
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

static void stream_style_sharded_admission(native_io_sharded_context *context, int status,
                                         native_io_sharded_request request, void *arg) {
  stream_style_sharded_operation *state = (stream_style_sharded_operation *)arg;
  (void)context;
  state->admission_status = status;
  state->request = request;
}

static void stream_style_sharded_terminal(
    native_io_sharded_context *context,
    const native_io_sharded_completion *completion, void *arg) {
  stream_style_sharded_operation *state = (stream_style_sharded_operation *)arg;
  (void)context;
  state->kind = completion->kind;
  state->terminal_status = completion->status;
  state->bytes = completion->bytes;
  state->user_data = completion->user_data;
  ++state->terminal_count;
}

static void stream_style_sharded_finalize(void *arg) {
  stream_style_sharded_operation *state = (stream_style_sharded_operation *)arg;
  ++state->finalize_count;
}

static void stream_style_sharded_reset_operation(stream_style_sharded_operation *state) {
  memset(state, 0, sizeof(*state));
  state->admission_status = SALTS_EIO;
  state->terminal_status = SALTS_EIO;
}

static int stream_style_sharded_submit_operation(
    stream_style_sharded_fixture *fixture, size_t endpoint_index,
    native_io_operation_kind kind, unsigned char *buffer, size_t length,
    uintptr_t user_data, stream_style_sharded_operation *state) {
  const native_io_sharded_operation operation = {
      .kind = kind,
      .endpoint = fixture->endpoints[endpoint_index],
      .buffer = buffer,
      .length = length,
      .user_data = user_data};
  const native_io_sharded_ownership ownership = {
      stream_style_sharded_terminal, stream_style_sharded_finalize, state};
  stream_style_sharded_reset_operation(state);
  return native_io_sharded_submit_owned(
      fixture->runtime, &operation, &ownership,
      stream_style_sharded_admission, state);
}

static int stream_style_sharded_consume_terminal(
    stream_style_sharded_operation *state, size_t remaining, size_t *offset) {
  if (state->terminal_count == 0u) return SALTS_ETIMEDOUT;
  if (state->terminal_count != 1u || state->finalize_count != 1u)
    return SALTS_EPROTO;
  if (state->kind != NATIVE_IO_COMPLETION_OK || state->bytes == 0u ||
      state->bytes > remaining)
    return state->terminal_status != SALTS_OK ? state->terminal_status : SALTS_EIO;
  *offset += state->bytes;
  return SALTS_OK;
}

static void stream_style_sharded_observe_task(native_io_sharded_context *context, void *arg) {
  stream_style_sharded_observe *observe = (stream_style_sharded_observe *)arg;
  observe->count = 0u;
  observe->status = native_io_sharded_context_observe(
      context, observe->events, STREAM_STYLE_COMPLETION_CAPACITY,
      STREAM_STYLE_TIMEOUT_MS, &observe->count);
  if (observe->status == SALTS_OK && observe->fixture != NULL)
    stream_style_record_batch(
        &observe->fixture->observe_calls, &observe->fixture->completion_count,
        &observe->fixture->completion_batches,
        &observe->fixture->max_completion_batch, observe->count);
}

static int stream_style_sharded_transfer(
    stream_style_sharded_fixture *fixture, native_io_sharded_context *same_owner_context) {
  size_t read_offset = 0u;
  size_t write_offset = 0u;

  memset(fixture->received, 0, fixture->payload_size);
  while (read_offset < fixture->payload_size ||
         write_offset < fixture->payload_size) {
    stream_style_sharded_operation read_state;
    stream_style_sharded_operation write_state;
    bool read_pending = false;
    bool write_pending = false;
    int status = SALTS_OK;

    if (read_offset < fixture->payload_size) {
      status = stream_style_sharded_submit_operation(
          fixture, 0u, NATIVE_IO_OPERATION_STREAM_RECV,
          fixture->received + read_offset,
          fixture->payload_size - read_offset, 1u, &read_state);
      if (status != SALTS_OK) return status;
      read_pending = true;
    }
    if (write_offset < fixture->payload_size) {
      status = stream_style_sharded_submit_operation(
          fixture, 1u, NATIVE_IO_OPERATION_STREAM_SEND,
          fixture->sent + write_offset,
          fixture->payload_size - write_offset, 2u, &write_state);
      if (status != SALTS_OK) return status;
      write_pending = true;
    }

    if (same_owner_context == NULL) {
      status = native_io_sharded_wait(fixture->runtime);
      if (status != SALTS_OK) return status;
    }

    if (read_pending && read_state.admission_status != SALTS_OK)
      return read_state.admission_status;
    if (write_pending && write_state.admission_status != SALTS_OK)
      return write_state.admission_status;

    while (read_pending || write_pending) {
      stream_style_sharded_observe observe = {.fixture = fixture};
      if (same_owner_context != NULL) {
        stream_style_sharded_observe_task(same_owner_context, &observe);
      } else {
        native_io_sharded_task observe_task = {
            stream_style_sharded_observe_task, NULL, NULL, &observe};
        status = native_io_sharded_submit_to(fixture->runtime, 1u, &observe_task);
        if (status == SALTS_OK) status = native_io_sharded_wait(fixture->runtime);
        if (status != SALTS_OK) return status;
      }

      if (observe.status != SALTS_OK) return observe.status;
      if (observe.count == 0u) return SALTS_EIO;

      if (read_pending && read_state.terminal_count != 0u) {
        status = stream_style_sharded_consume_terminal(
            &read_state, fixture->payload_size - read_offset, &read_offset);
        if (status != SALTS_OK) return status;
        read_pending = false;
      }
      if (write_pending && write_state.terminal_count != 0u) {
        status = stream_style_sharded_consume_terminal(
            &write_state, fixture->payload_size - write_offset, &write_offset);
        if (status != SALTS_OK) return status;
        write_pending = false;
      }
    }
  }

  return memcmp(fixture->sent, fixture->received, fixture->payload_size) == 0
             ? SALTS_OK
             : SALTS_EIO;
}

static uint64_t stream_style_stats_delta(uint64_t after, uint64_t before) {
  return after >= before ? after - before : 0u;
}

static void stream_style_same_driver_run(native_io_sharded_context *context, void *arg) {
  stream_style_same_driver *driver = (stream_style_same_driver *)arg;
  stream_style_sharded_fixture *fixture = driver->fixture;

  driver->status = SALTS_OK;
  for (size_t index = 0u; index < STREAM_STYLE_WARMUP_TRANSFERS; ++index) {
    driver->status = stream_style_sharded_transfer(fixture, context);
    if (driver->status != SALTS_OK) return;
  }

  stream_style_reset_sharded_completion_stats(fixture);
  if (!native_io_sharded_get_stats(fixture->runtime, &driver->before)) {
    driver->status = SALTS_EIO;
    return;
  }

  if (driver->trace)
    stream_style_trace_marker(true, STREAM_STYLE_SHARDED_SAME_OWNER,
                              fixture->payload_size);
  for (size_t index = 0u; index < STREAM_STYLE_MEASURED_TRANSFERS; ++index) {
    const uint64_t started = salts_hrtime();
    driver->status = stream_style_sharded_transfer(fixture, context);
    driver->latencies[index] = salts_hrtime() - started;
    if (driver->status != SALTS_OK) return;
  }
  if (driver->trace)
    stream_style_trace_marker(false, STREAM_STYLE_SHARDED_SAME_OWNER,
                              fixture->payload_size);

  if (!native_io_sharded_get_stats(fixture->runtime, &driver->after))
    driver->status = SALTS_EIO;
}

static int stream_style_measure_sharded(native_io_backend_kind kind,
                                      stream_style_kind style,
                                      size_t payload_size,
                                      stream_style_result *out,
                                      bool trace) {
  stream_style_sharded_fixture fixture;
  uint64_t *latencies =
      (uint64_t *)calloc(STREAM_STYLE_MEASURED_TRANSFERS, sizeof(*latencies));
  native_io_sharded_stats before = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
  native_io_sharded_stats after = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
  int status;

  if (latencies == NULL) return SALTS_ENOMEM;
  status = stream_style_sharded_fixture_init(&fixture, kind, payload_size);
  if (status != SALTS_OK) goto cleanup;

  if (style == STREAM_STYLE_SHARDED_SAME_OWNER) {
    stream_style_same_driver driver = {
        &fixture, latencies,
        NATIVE_IO_SHARDED_STATS_V1_INITIALIZER,
        NATIVE_IO_SHARDED_STATS_V1_INITIALIZER,
        trace,
        SALTS_OK};
    native_io_sharded_task task = {
        stream_style_same_driver_run, NULL, NULL, &driver};
    status = native_io_sharded_submit_to(fixture.runtime, 1u, &task);
    if (status == SALTS_OK) status = native_io_sharded_wait(fixture.runtime);
    if (status == SALTS_OK) status = driver.status;
    before = driver.before;
    after = driver.after;
  } else {
    for (size_t index = 0u; index < STREAM_STYLE_WARMUP_TRANSFERS; ++index) {
      status = stream_style_sharded_transfer(&fixture, NULL);
      if (status != SALTS_OK) goto cleanup;
    }
    stream_style_reset_sharded_completion_stats(&fixture);
    if (!native_io_sharded_get_stats(fixture.runtime, &before)) {
      status = SALTS_EIO;
      goto cleanup;
    }
    if (trace)
      stream_style_trace_marker(true, STREAM_STYLE_SHARDED_CROSS_OWNER,
                                payload_size);
    for (size_t index = 0u; index < STREAM_STYLE_MEASURED_TRANSFERS; ++index) {
      const uint64_t started = salts_hrtime();
      status = stream_style_sharded_transfer(&fixture, NULL);
      latencies[index] = salts_hrtime() - started;
      if (status != SALTS_OK) goto cleanup;
    }
    if (trace)
      stream_style_trace_marker(false, STREAM_STYLE_SHARDED_CROSS_OWNER,
                                payload_size);
    if (!native_io_sharded_get_stats(fixture.runtime, &after)) {
      status = SALTS_EIO;
      goto cleanup;
    }
  }

  if (status == SALTS_OK) {
    memset(out, 0, sizeof(*out));
    out->style = stream_style_name(style);
    out->payload_size = payload_size;
    out->transfers = STREAM_STYLE_MEASURED_TRANSFERS;
    for (size_t index = 0u; index < STREAM_STYLE_MEASURED_TRANSFERS; ++index)
      out->wall_ns += latencies[index];
    out->queued_dispatches =
        stream_style_stats_delta(after.queued_dispatches, before.queued_dispatches);
    out->rejected_tasks =
        stream_style_stats_delta(after.rejected_tasks, before.rejected_tasks);
    {
      const uint64_t direct_tasks =
          stream_style_stats_delta(after.same_shard_direct_tasks,
                                 before.same_shard_direct_tasks);
      out->same_owner_direct_tasks_per_transfer =
          (double)direct_tasks / (double)STREAM_STYLE_MEASURED_TRANSFERS;
    }
    out->message_hops_per_transfer =
        (double)out->queued_dispatches /
        (double)STREAM_STYLE_MEASURED_TRANSFERS;
    out->peak_command_slots = after.peak_command_slots;
    out->observe_calls = fixture.observe_calls;
    out->completion_count = fixture.completion_count;
    out->completion_batches = fixture.completion_batches;
    out->max_completion_batch = fixture.max_completion_batch;
    status = stream_style_finalize_result(out, latencies);
  }

cleanup:
  {
    const int destroy_status = stream_style_sharded_fixture_destroy(&fixture);
    if (status == SALTS_OK) status = destroy_status;
  }
  free(latencies);
  return status;
}

static FILE *stream_style_open_csv(void) {
  const char *prefix = getenv("NATIVE_IO_STREAM_STYLE_OUTPUT");
  char path[1024];
  if (prefix == NULL || *prefix == '\0') return NULL;
  if (snprintf(path, sizeof(path), "%s.csv", prefix) < 0) return NULL;
  return fopen(path, "w");
}

static void stream_style_print_csv_row(FILE *stream, const char *backend,
                                     const stream_style_result *result) {
  fprintf(stream,
          "%s,%s,%zu,%zu,%.6f,%.6f,%.6f,%.6f,%.6f,%" PRIu64
          ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
          ",%" PRIu64 ",%" PRIu64 "\n",
          backend, result->style, result->payload_size, result->transfers,
          (double)result->p50_ns / 1000.0,
          (double)result->p95_ns / 1000.0,
          result->mib_per_second,
          result->message_hops_per_transfer,
          result->same_owner_direct_tasks_per_transfer,
          result->queued_dispatches,
          result->rejected_tasks,
          result->peak_command_slots,
          result->observe_calls,
          result->completion_count,
          result->completion_batches,
          result->max_completion_batch);
}

static bool stream_style_vector_benchmark_enabled(void) {
  const char *value = getenv("NATIVE_IO_STREAM_VECTOR_BENCHMARK");
  return value != NULL && *value != '\0';
}

static int stream_style_run_vector_benchmark(native_io_backend_kind kind,
                                             const char *backend) {
  const stream_style_kind styles[] = {STREAM_STYLE_DIRECT, STREAM_STYLE_COROUTINE};
  const char *names[] = {"direct_vector", "coroutine_vector"};
  const size_t payload_count =
      sizeof(STREAM_STYLE_PAYLOADS) / sizeof(STREAM_STYLE_PAYLOADS[0]);

  printf("# NativeIO bounded vector-write benchmark\n\n");
  printf("Backend: %s\n\n", backend);
  printf("| style | payload | p50 us | p95 us | MiB/s | observe calls | completions | max batch |\n");
  printf("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t style_index = 0u; style_index < 2u; ++style_index) {
    for (size_t payload_index = 0u; payload_index < payload_count; ++payload_index) {
      stream_style_result result = {0};
      const int status =
          stream_style_measure_backend(kind, styles[style_index],
                                       STREAM_STYLE_PAYLOADS[payload_index],
                                       &result, false, true);
      if (status != SALTS_OK) return status;
      printf("| %s | %zu | %.3f | %.3f | %.2f | %" PRIu64 " | %" PRIu64
             " | %" PRIu64 " |\n",
             names[style_index], result.payload_size,
             (double)result.p50_ns / 1000.0,
             (double)result.p95_ns / 1000.0,
             result.mib_per_second,
             result.observe_calls,
             result.completion_count,
             result.max_completion_batch);
    }
  }
  return SALTS_OK;
}

int main(void) {
  const native_io_backend_kind kind = stream_style_backend();
  const char *backend = stream_style_backend_name(kind);
  const stream_style_kind styles[] = {
      STREAM_STYLE_DIRECT,
      STREAM_STYLE_COROUTINE,
      STREAM_STYLE_SHARDED_SAME_OWNER,
      STREAM_STYLE_SHARDED_CROSS_OWNER};
  const size_t style_count = sizeof(styles) / sizeof(styles[0]);
  const size_t payload_count =
      sizeof(STREAM_STYLE_PAYLOADS) / sizeof(STREAM_STYLE_PAYLOADS[0]);
  stream_style_result results[4][4];
  stream_style_trace_selection trace = {0};
  FILE *csv = NULL;
  int trace_status;

  if (kind == (native_io_backend_kind)0 ||
      !native_io_backend_kind_supported(kind)) {
    fprintf(stderr, "unsupported NativeIO TCP STREAM style backend: %s\n", backend);
    return 2;
  }

  {
    const int network_status = stream_style_network_start();
    if (network_status != SALTS_OK) {
      fprintf(stderr, "TCP network initialization failed: %d\n", network_status);
      return 1;
    }
  }

  if (stream_style_vector_benchmark_enabled()) {
    const int status = stream_style_run_vector_benchmark(kind, backend);
    stream_style_network_stop();
    return status == SALTS_OK ? 0 : 1;
  }

  trace_status = stream_style_parse_trace(&trace);
  if (trace_status != SALTS_OK) {
    fprintf(stderr, "invalid NATIVE_IO_STREAM_STYLE_TRACE: %d\n", trace_status);
    stream_style_network_stop();
    return 2;
  }
  if (trace.enabled) {
    bool payload_supported = false;
    stream_style_result result = {0};
    int status;
    for (size_t index = 0u; index < payload_count; ++index)
      if (STREAM_STYLE_PAYLOADS[index] == trace.payload_size)
        payload_supported = true;
    if (!payload_supported) {
      fprintf(stderr, "unsupported trace payload: %zu\n", trace.payload_size);
      stream_style_network_stop();
      return 2;
    }
    status = trace.style == STREAM_STYLE_DIRECT ||
                     trace.style == STREAM_STYLE_COROUTINE
                 ? stream_style_measure_backend(kind, trace.style,
                                                trace.payload_size, &result, true, false)
                 : stream_style_measure_sharded(kind, trace.style,
                                                trace.payload_size, &result, true);
    stream_style_network_stop();
    return status == SALTS_OK ? 0 : 1;
  }

  memset(results, 0, sizeof(results));
  for (size_t style_index = 0u; style_index < style_count; ++style_index) {
    for (size_t payload_index = 0u; payload_index < payload_count; ++payload_index) {
      const stream_style_kind style = styles[style_index];
      int status;
      if (style == STREAM_STYLE_DIRECT || style == STREAM_STYLE_COROUTINE)
        status = stream_style_measure_backend(kind, style,
                                            STREAM_STYLE_PAYLOADS[payload_index],
                                            &results[style_index][payload_index], false, false);
      else
        status = stream_style_measure_sharded(kind, style,
                                            STREAM_STYLE_PAYLOADS[payload_index],
                                            &results[style_index][payload_index], false);
      if (status != SALTS_OK) {
        fprintf(stderr, "%s %s %zu-byte benchmark failed: %d\n",
                backend, stream_style_name(style),
                STREAM_STYLE_PAYLOADS[payload_index], status);
        stream_style_network_stop();
        return 1;
      }
    }
  }

  printf("# NativeIO TCP STREAM execution-style benchmark\n\n");
  printf("Backend: %s\n\n", backend);
  printf("Each cell measures %d complete one-way transfers after %d warmups. "
         "Latency percentiles are individual complete-transfer latencies.\n\n",
         STREAM_STYLE_MEASURED_TRANSFERS, STREAM_STYLE_WARMUP_TRANSFERS);
  printf("| style | payload | p50 us | p95 us | MiB/s | message hops/transfer | "
         "same-owner direct tasks/transfer | queued dispatches | rejected | peak command slots | "
         "observe calls | completions | completion batches | max batch |\n");
  printf("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | "
         "---: | ---: | ---: | ---: |\n");
  for (size_t style_index = 0u; style_index < style_count; ++style_index) {
    for (size_t payload_index = 0u; payload_index < payload_count; ++payload_index) {
      const stream_style_result *result = &results[style_index][payload_index];
      printf("| %s | %zu | %.3f | %.3f | %.2f | %.3f | %.3f | %" PRIu64
             " | %" PRIu64 " | %" PRIu64 " | %" PRIu64 " | %" PRIu64
             " | %" PRIu64 " | %" PRIu64 " |\n",
             result->style, result->payload_size,
             (double)result->p50_ns / 1000.0,
             (double)result->p95_ns / 1000.0,
             result->mib_per_second,
             result->message_hops_per_transfer,
             result->same_owner_direct_tasks_per_transfer,
             result->queued_dispatches,
             result->rejected_tasks,
             result->peak_command_slots,
             result->observe_calls,
             result->completion_count,
             result->completion_batches,
             result->max_completion_batch);
    }
  }

  csv = stream_style_open_csv();
  if (csv != NULL) {
    fprintf(csv,
            "backend,style,payload_bytes,transfers,p50_us,p95_us,mib_per_second,"
            "message_hops_per_transfer,same_owner_direct_tasks_per_transfer,"
            "queued_dispatches,rejected_tasks,peak_command_slots,observe_calls,"
            "completion_count,completion_batches,max_completion_batch\n");
    for (size_t style_index = 0u; style_index < style_count; ++style_index)
      for (size_t payload_index = 0u; payload_index < payload_count; ++payload_index)
        stream_style_print_csv_row(csv, backend,
                                 &results[style_index][payload_index]);
    fclose(csv);
  }

  stream_style_network_stop();
  return 0;
}
