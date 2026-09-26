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

#include <errno.h>
#include <fcntl.h>
#include <linux/vm_sockets.h>
#include <sys/socket.h>
#include <unistd.h>

typedef int vsock_style_socket;
#define VSOCK_STYLE_INVALID_SOCKET (-1)

enum {
  VSOCK_STYLE_WARMUP_TRANSFERS = 32,
  VSOCK_STYLE_MEASURED_TRANSFERS = 512,
  VSOCK_STYLE_TIMEOUT_MS = 5000,
  VSOCK_STYLE_REQUEST_CAPACITY = 4,
  VSOCK_STYLE_COMPLETION_CAPACITY = 4,
  VSOCK_STYLE_SHARDED_QUEUE_CAPACITY = 64
};

static const size_t VSOCK_STYLE_PAYLOADS[] = {1024u, 8192u, 32768u, 65536u};

typedef enum vsock_style_kind {
  VSOCK_STYLE_DIRECT = 0,
  VSOCK_STYLE_COROUTINE,
  VSOCK_STYLE_SHARDED_SAME_OWNER,
  VSOCK_STYLE_SHARDED_CROSS_OWNER
} vsock_style_kind;

typedef struct vsock_style_result {
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
} vsock_style_result;

typedef struct vsock_style_backend_fixture {
  native_io_backend backend;
  native_io_endpoint endpoints[2];
  vsock_style_socket sockets[2];
  unsigned char *sent;
  unsigned char *received;
  size_t payload_size;
} vsock_style_backend_fixture;

typedef struct vsock_style_coroutine_operation {
  vsock_style_backend_fixture *fixture;
  unsigned char *buffer;
  size_t length;
  size_t offset;
  int status;
  bool write;
  bool done;
} vsock_style_coroutine_operation;

typedef struct vsock_style_sharded_fixture {
  native_io_sharded *runtime;
  native_io_sharded_endpoint endpoints[2];
  vsock_style_socket sockets[2];
  unsigned char *sent;
  unsigned char *received;
  size_t payload_size;
  int attach_status;
  int release_status;
} vsock_style_sharded_fixture;

typedef struct vsock_style_sharded_operation {
  int admission_status;
  native_io_sharded_request request;
  native_io_completion_kind kind;
  int terminal_status;
  size_t bytes;
  uintptr_t user_data;
  unsigned terminal_count;
  unsigned finalize_count;
} vsock_style_sharded_operation;

typedef struct vsock_style_sharded_observe {
  native_io_sharded_completion events[VSOCK_STYLE_COMPLETION_CAPACITY];
  size_t count;
  int status;
} vsock_style_sharded_observe;

typedef struct vsock_style_same_driver {
  vsock_style_sharded_fixture *fixture;
  uint64_t *latencies;
  native_io_sharded_stats before;
  native_io_sharded_stats after;
  int status;
} vsock_style_same_driver;

static native_io_backend_kind vsock_style_backend(void) {
  const char *value = getenv("NATIVE_IO_VSOCK_STYLE_BACKEND");
  if (value == NULL || *value == '\0') return NATIVE_IO_BACKEND_EPOLL;
  if (strcmp(value, "epoll") == 0) return NATIVE_IO_BACKEND_EPOLL;
  if (strcmp(value, "io_uring") == 0) return NATIVE_IO_BACKEND_IO_URING;
  return (native_io_backend_kind)0;
}

static const char *vsock_style_backend_name(native_io_backend_kind kind) {
  switch (kind) {
    case NATIVE_IO_BACKEND_EPOLL: return "epoll";
    case NATIVE_IO_BACKEND_IO_URING: return "io_uring";
    case NATIVE_IO_BACKEND_IOCP: return "iocp";
    case NATIVE_IO_BACKEND_KQUEUE: return "kqueue";
    default: return "unsupported";
  }
}

static const char *vsock_style_name(vsock_style_kind style) {
  switch (style) {
    case VSOCK_STYLE_DIRECT: return "direct";
    case VSOCK_STYLE_COROUTINE: return "coroutine";
    case VSOCK_STYLE_SHARDED_SAME_OWNER: return "sharded_same_owner";
    case VSOCK_STYLE_SHARDED_CROSS_OWNER: return "sharded_cross_owner";
    default: return "unknown";
  }
}

static bool vsock_style_unavailable(int error) {
  return error == EAFNOSUPPORT || error == EPROTONOSUPPORT ||
         error == ESOCKTNOSUPPORT || error == ENODEV || error == EPERM ||
         error == EADDRNOTAVAIL;
}

static int vsock_style_socket_error(void) {
  const int error = errno;
  return error == 0 ? SALTS_EIO : -error;
}

static void vsock_style_close_socket(vsock_style_socket socket_value) {
  if (socket_value >= 0) (void)close(socket_value);
}

static int vsock_style_set_nonblocking(vsock_style_socket socket_value) {
  const int flags = fcntl(socket_value, F_GETFL, 0);
  if (flags < 0) return vsock_style_socket_error();
  if ((flags & O_NONBLOCK) != 0) return SALTS_OK;
  return fcntl(socket_value, F_SETFL, flags | O_NONBLOCK) == 0
             ? SALTS_OK
             : vsock_style_socket_error();
}

static int vsock_style_make_pair(vsock_style_socket sockets[2]) {
  struct sockaddr_vm local;
  struct sockaddr_vm target;
  socklen_t local_length = (socklen_t)sizeof(local);
  int listener = -1;
  int peer = -1;
  int accepted = -1;
  int status = SALTS_OK;
  int error;

  if (sockets == NULL) return SALTS_EINVAL;
  sockets[0] = VSOCK_STYLE_INVALID_SOCKET;
  sockets[1] = VSOCK_STYLE_INVALID_SOCKET;

  listener = socket(AF_VSOCK, SOCK_STREAM, 0);
  if (listener < 0) {
    error = errno;
    return vsock_style_unavailable(error) ? SALTS_ENOTSUP : -error;
  }

  memset(&local, 0, sizeof(local));
  local.svm_family = AF_VSOCK;
  local.svm_cid = VMADDR_CID_ANY;
  local.svm_port = VMADDR_PORT_ANY;
  if (bind(listener, (const struct sockaddr *)&local,
           (socklen_t)sizeof(local)) != 0) {
    error = errno;
    (void)close(listener);
    return vsock_style_unavailable(error) ? SALTS_ENOTSUP : -error;
  }
  if (listen(listener, 2) != 0) {
    error = errno;
    (void)close(listener);
    return -error;
  }
  if (getsockname(listener, (struct sockaddr *)&local, &local_length) != 0 ||
      local_length < sizeof(local) || local.svm_family != AF_VSOCK ||
      local.svm_port == VMADDR_PORT_ANY) {
    error = errno != 0 ? errno : EIO;
    (void)close(listener);
    return -error;
  }

  peer = socket(AF_VSOCK, SOCK_STREAM, 0);
  if (peer < 0) {
    error = errno;
    (void)close(listener);
    return vsock_style_unavailable(error) ? SALTS_ENOTSUP : -error;
  }

  memset(&target, 0, sizeof(target));
  target.svm_family = AF_VSOCK;
  target.svm_cid = VMADDR_CID_LOCAL;
  target.svm_port = local.svm_port;
  if (connect(peer, (const struct sockaddr *)&target,
              (socklen_t)sizeof(target)) != 0) {
    error = errno;
    (void)close(peer);
    (void)close(listener);
    return vsock_style_unavailable(error) ? SALTS_ENOTSUP : -error;
  }

  accepted = accept(listener, NULL, NULL);
  if (accepted < 0) {
    error = errno;
    (void)close(peer);
    (void)close(listener);
    return -error;
  }
  (void)close(listener);

  status = vsock_style_set_nonblocking(accepted);
  if (status == SALTS_OK) status = vsock_style_set_nonblocking(peer);
  if (status != SALTS_OK) {
    (void)close(accepted);
    (void)close(peer);
    return status;
  }

  sockets[0] = accepted;
  sockets[1] = peer;
  return SALTS_OK;
}

static int vsock_style_compare_u64(const void *left, const void *right) {
  const uint64_t a = *(const uint64_t *)left;
  const uint64_t b = *(const uint64_t *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static int vsock_style_finalize_result(vsock_style_result *result, uint64_t *latencies) {
  if (result == NULL || latencies == NULL || result->transfers == 0u) return SALTS_EINVAL;
  qsort(latencies, result->transfers, sizeof(*latencies), vsock_style_compare_u64);
  result->p50_ns = latencies[(result->transfers - 1u) * 50u / 100u];
  result->p95_ns = latencies[(result->transfers - 1u) * 95u / 100u];
  result->mib_per_second =
      result->wall_ns == 0u
          ? 0.0
          : (double)result->payload_size * (double)result->transfers * 1.0e9 /
                (double)result->wall_ns / (1024.0 * 1024.0);
  return SALTS_OK;
}

static int vsock_style_backend_fixture_init(vsock_style_backend_fixture *fixture,
                                           native_io_backend_kind kind,
                                           size_t payload_size) {
  const native_io_backend_config config = {
      kind, 2u, VSOCK_STYLE_REQUEST_CAPACITY, VSOCK_STYLE_COMPLETION_CAPACITY};
  int status;

  memset(fixture, 0, sizeof(*fixture));
  fixture->sockets[0] = VSOCK_STYLE_INVALID_SOCKET;
  fixture->sockets[1] = VSOCK_STYLE_INVALID_SOCKET;
  fixture->payload_size = payload_size;
  fixture->sent = (unsigned char *)malloc(payload_size);
  fixture->received = (unsigned char *)malloc(payload_size);
  if (fixture->sent == NULL || fixture->received == NULL) return SALTS_ENOMEM;
  memset(fixture->sent, 0x5au, payload_size);
  memset(fixture->received, 0, payload_size);

  status = vsock_style_make_pair(fixture->sockets);
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

static int vsock_style_backend_fixture_destroy(vsock_style_backend_fixture *fixture) {
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
    if (fixture->sockets[index] != VSOCK_STYLE_INVALID_SOCKET) {
      vsock_style_close_socket(fixture->sockets[index]);
      fixture->sockets[index] = VSOCK_STYLE_INVALID_SOCKET;
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

static int vsock_style_direct_transfer(vsock_style_backend_fixture *fixture) {
  size_t sent_offset = 0u;
  size_t received_offset = 0u;
  bool read_pending = false;
  bool write_pending = false;

  memset(fixture->received, 0, fixture->payload_size);
  while (sent_offset < fixture->payload_size || received_offset < fixture->payload_size) {
    native_io_completion events[VSOCK_STYLE_COMPLETION_CAPACITY];
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
      const native_io_operation operation = {
          .kind = NATIVE_IO_OPERATION_STREAM_SEND,
          .endpoint = fixture->endpoints[1],
          .buffer = fixture->sent + sent_offset,
          .length = fixture->payload_size - sent_offset,
          .user_data = 2u};
      native_io_request request = {0};
      status = native_io_backend_submit(&fixture->backend, &operation, &request);
      if (status != SALTS_OK) return status;
      write_pending = true;
    }

    status = native_io_backend_observe(&fixture->backend, events,
                                       VSOCK_STYLE_COMPLETION_CAPACITY,
                                       VSOCK_STYLE_TIMEOUT_MS, &count);
    if (status != SALTS_OK) return status;
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

static void vsock_style_coroutine_entry(native_io_coroutine *coroutine, void *arg) {
  vsock_style_coroutine_operation *state = (vsock_style_coroutine_operation *)arg;
  state->status = SALTS_OK;
  while (state->offset < state->length) {
    native_io_completion completion = {0};
    const native_io_operation operation = {
        .kind = state->write ? NATIVE_IO_OPERATION_STREAM_SEND
                             : NATIVE_IO_OPERATION_STREAM_RECV,
        .endpoint = state->fixture->endpoints[state->write ? 1u : 0u],
        .buffer = state->buffer + state->offset,
        .length = state->length - state->offset,
        .user_data = state->write ? 2u : 1u};
    state->status =
        native_io_coroutine_await(coroutine, &operation, &completion);
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

static int vsock_style_coroutine_cancel_and_drain(
    vsock_style_backend_fixture *fixture, native_io_coroutine_task task,
    vsock_style_coroutine_operation *state) {
  native_io_completion events[VSOCK_STYLE_COMPLETION_CAPACITY];
  int status;

  if (state->done || !native_io_coroutine_task_valid(task)) return SALTS_OK;
  status = native_io_backend_cancel_coroutine(&fixture->backend, task);
  if (status != SALTS_OK && status != SALTS_EALREADY) return status;
  while (!state->done) {
    size_t count = 0u;
    status = native_io_backend_observe(&fixture->backend, events,
                                       VSOCK_STYLE_COMPLETION_CAPACITY,
                                       VSOCK_STYLE_TIMEOUT_MS, &count);
    if (status != SALTS_OK) return status;
    if (count != 0u) return SALTS_EPROTO;
  }
  return SALTS_OK;
}

static int vsock_style_coroutine_transfer(vsock_style_backend_fixture *fixture) {
  vsock_style_coroutine_operation read_state = {
      fixture, fixture->received, fixture->payload_size, 0u, SALTS_OK, false, false};
  vsock_style_coroutine_operation write_state = {
      fixture, fixture->sent, fixture->payload_size, 0u, SALTS_OK, true, false};
  native_io_coroutine_task read_task = {0};
  native_io_coroutine_task write_task = {0};
  native_io_completion events[VSOCK_STYLE_COMPLETION_CAPACITY];
  int status;

  memset(fixture->received, 0, fixture->payload_size);
  status = native_io_backend_spawn_coroutine(&fixture->backend,
                                             vsock_style_coroutine_entry,
                                             &read_state, &read_task);
  if (status == SALTS_OK)
    status = native_io_backend_spawn_coroutine(&fixture->backend,
                                               vsock_style_coroutine_entry,
                                               &write_state, &write_task);

  while (status == SALTS_OK && (!read_state.done || !write_state.done)) {
    size_t count = 0u;
    status = native_io_backend_observe(&fixture->backend, events,
                                       VSOCK_STYLE_COMPLETION_CAPACITY,
                                       VSOCK_STYLE_TIMEOUT_MS, &count);
    if (status == SALTS_OK && count != 0u) status = SALTS_EPROTO;
  }

  if (status != SALTS_OK) {
    const int failure = status;
    const int write_drain =
        vsock_style_coroutine_cancel_and_drain(fixture, write_task, &write_state);
    const int read_drain =
        vsock_style_coroutine_cancel_and_drain(fixture, read_task, &read_state);
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

static int vsock_style_measure_backend(native_io_backend_kind kind, vsock_style_kind style,
                                      size_t payload_size, vsock_style_result *out) {
  vsock_style_backend_fixture fixture;
  uint64_t *latencies =
      (uint64_t *)calloc(VSOCK_STYLE_MEASURED_TRANSFERS, sizeof(*latencies));
  int status;

  if (latencies == NULL) return SALTS_ENOMEM;
  status = vsock_style_backend_fixture_init(&fixture, kind, payload_size);
  if (status != SALTS_OK) goto cleanup;

  for (size_t index = 0u; index < VSOCK_STYLE_WARMUP_TRANSFERS; ++index) {
    status = style == VSOCK_STYLE_DIRECT ? vsock_style_direct_transfer(&fixture)
                                        : vsock_style_coroutine_transfer(&fixture);
    if (status != SALTS_OK) goto cleanup;
  }

  memset(out, 0, sizeof(*out));
  out->style = vsock_style_name(style);
  out->payload_size = payload_size;
  out->transfers = VSOCK_STYLE_MEASURED_TRANSFERS;
  for (size_t index = 0u; index < VSOCK_STYLE_MEASURED_TRANSFERS; ++index) {
    const uint64_t started = salts_hrtime();
    status = style == VSOCK_STYLE_DIRECT ? vsock_style_direct_transfer(&fixture)
                                        : vsock_style_coroutine_transfer(&fixture);
    latencies[index] = salts_hrtime() - started;
    if (status != SALTS_OK) goto cleanup;
    out->wall_ns += latencies[index];
  }
  status = vsock_style_finalize_result(out, latencies);

cleanup:
  {
    const int destroy_status = vsock_style_backend_fixture_destroy(&fixture);
    if (status == SALTS_OK) status = destroy_status;
  }
  free(latencies);
  return status;
}

static void vsock_style_sharded_attach(native_io_sharded_context *context, void *arg) {
  vsock_style_sharded_fixture *fixture = (vsock_style_sharded_fixture *)arg;
  fixture->attach_status = native_io_sharded_context_attach_socket(
      context, (uintptr_t)fixture->sockets[0], &fixture->endpoints[0]);
  if (fixture->attach_status == SALTS_OK)
    fixture->attach_status = native_io_sharded_context_attach_socket(
      context, (uintptr_t)fixture->sockets[1], &fixture->endpoints[1]);
}

static void vsock_style_sharded_release(native_io_sharded_context *context, void *arg) {
  vsock_style_sharded_fixture *fixture = (vsock_style_sharded_fixture *)arg;
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

static int vsock_style_sharded_fixture_init(vsock_style_sharded_fixture *fixture,
                                           native_io_backend_kind kind,
                                           size_t payload_size) {
  const native_io_sharded_config config = {
      2u, VSOCK_STYLE_SHARDED_QUEUE_CAPACITY,
      {kind, 2u, VSOCK_STYLE_REQUEST_CAPACITY, VSOCK_STYLE_COMPLETION_CAPACITY}};
  native_io_sharded_task attach_task;
  int status;

  memset(fixture, 0, sizeof(*fixture));
  fixture->sockets[0] = VSOCK_STYLE_INVALID_SOCKET;
  fixture->sockets[1] = VSOCK_STYLE_INVALID_SOCKET;
  fixture->payload_size = payload_size;
  fixture->sent = (unsigned char *)malloc(payload_size);
  fixture->received = (unsigned char *)malloc(payload_size);
  if (fixture->sent == NULL || fixture->received == NULL) return SALTS_ENOMEM;
  memset(fixture->sent, 0x5au, payload_size);
  memset(fixture->received, 0, payload_size);

  status = vsock_style_make_pair(fixture->sockets);
  if (status != SALTS_OK) return status;
  status = native_io_sharded_create(&config, &fixture->runtime);
  if (status != SALTS_OK) return status;

  attach_task =
      (native_io_sharded_task){vsock_style_sharded_attach, NULL, NULL, fixture};
  status = native_io_sharded_submit_to(fixture->runtime, 1u, &attach_task);
  if (status == SALTS_OK) status = native_io_sharded_wait(fixture->runtime);
  if (status == SALTS_OK) status = fixture->attach_status;
  return status;
}

static int vsock_style_sharded_fixture_destroy(vsock_style_sharded_fixture *fixture) {
  int status = SALTS_OK;

  for (size_t index = 0u; index < 2u; ++index) {
    if (fixture->sockets[index] != VSOCK_STYLE_INVALID_SOCKET) {
      vsock_style_close_socket(fixture->sockets[index]);
      fixture->sockets[index] = VSOCK_STYLE_INVALID_SOCKET;
    }
  }

  if (fixture->runtime != NULL) {
    native_io_sharded_task release_task = {
        vsock_style_sharded_release, NULL, NULL, fixture};
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

static void vsock_style_sharded_admission(native_io_sharded_context *context, int status,
                                         native_io_sharded_request request, void *arg) {
  vsock_style_sharded_operation *state = (vsock_style_sharded_operation *)arg;
  (void)context;
  state->admission_status = status;
  state->request = request;
}

static void vsock_style_sharded_terminal(
    native_io_sharded_context *context,
    const native_io_sharded_completion *completion, void *arg) {
  vsock_style_sharded_operation *state = (vsock_style_sharded_operation *)arg;
  (void)context;
  state->kind = completion->kind;
  state->terminal_status = completion->status;
  state->bytes = completion->bytes;
  state->user_data = completion->user_data;
  ++state->terminal_count;
}

static void vsock_style_sharded_finalize(void *arg) {
  vsock_style_sharded_operation *state = (vsock_style_sharded_operation *)arg;
  ++state->finalize_count;
}

static void vsock_style_sharded_reset_operation(vsock_style_sharded_operation *state) {
  memset(state, 0, sizeof(*state));
  state->admission_status = SALTS_EIO;
  state->terminal_status = SALTS_EIO;
}

static int vsock_style_sharded_submit_operation(
    vsock_style_sharded_fixture *fixture, size_t endpoint_index,
    native_io_operation_kind kind, unsigned char *buffer, size_t length,
    uintptr_t user_data, vsock_style_sharded_operation *state) {
  const native_io_sharded_operation operation = {
      .kind = kind,
      .endpoint = fixture->endpoints[endpoint_index],
      .buffer = buffer,
      .length = length,
      .user_data = user_data};
  const native_io_sharded_ownership ownership = {
      vsock_style_sharded_terminal, vsock_style_sharded_finalize, state};
  vsock_style_sharded_reset_operation(state);
  return native_io_sharded_submit_owned(
      fixture->runtime, &operation, &ownership,
      vsock_style_sharded_admission, state);
}

static int vsock_style_sharded_consume_terminal(
    vsock_style_sharded_operation *state, size_t remaining, size_t *offset) {
  if (state->terminal_count == 0u) return SALTS_ETIMEDOUT;
  if (state->terminal_count != 1u || state->finalize_count != 1u)
    return SALTS_EPROTO;
  if (state->kind != NATIVE_IO_COMPLETION_OK || state->bytes == 0u ||
      state->bytes > remaining)
    return state->terminal_status != SALTS_OK ? state->terminal_status : SALTS_EIO;
  *offset += state->bytes;
  return SALTS_OK;
}

static void vsock_style_sharded_observe_task(native_io_sharded_context *context, void *arg) {
  vsock_style_sharded_observe *observe = (vsock_style_sharded_observe *)arg;
  observe->count = 0u;
  observe->status = native_io_sharded_context_observe(
      context, observe->events, VSOCK_STYLE_COMPLETION_CAPACITY,
      VSOCK_STYLE_TIMEOUT_MS, &observe->count);
}

static int vsock_style_sharded_transfer(
    vsock_style_sharded_fixture *fixture, native_io_sharded_context *same_owner_context) {
  size_t read_offset = 0u;
  size_t write_offset = 0u;

  memset(fixture->received, 0, fixture->payload_size);
  while (read_offset < fixture->payload_size ||
         write_offset < fixture->payload_size) {
    vsock_style_sharded_operation read_state;
    vsock_style_sharded_operation write_state;
    bool read_pending = false;
    bool write_pending = false;
    int status = SALTS_OK;

    if (read_offset < fixture->payload_size) {
      status = vsock_style_sharded_submit_operation(
          fixture, 0u, NATIVE_IO_OPERATION_STREAM_RECV,
          fixture->received + read_offset,
          fixture->payload_size - read_offset, 1u, &read_state);
      if (status != SALTS_OK) return status;
      read_pending = true;
    }
    if (write_offset < fixture->payload_size) {
      status = vsock_style_sharded_submit_operation(
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
      vsock_style_sharded_observe observe = {0};
      if (same_owner_context != NULL) {
        vsock_style_sharded_observe_task(same_owner_context, &observe);
      } else {
        native_io_sharded_task observe_task = {
            vsock_style_sharded_observe_task, NULL, NULL, &observe};
        status = native_io_sharded_submit_to(fixture->runtime, 1u, &observe_task);
        if (status == SALTS_OK) status = native_io_sharded_wait(fixture->runtime);
        if (status != SALTS_OK) return status;
      }

      if (observe.status != SALTS_OK) return observe.status;
      if (observe.count == 0u) return SALTS_EIO;

      if (read_pending && read_state.terminal_count != 0u) {
        status = vsock_style_sharded_consume_terminal(
            &read_state, fixture->payload_size - read_offset, &read_offset);
        if (status != SALTS_OK) return status;
        read_pending = false;
      }
      if (write_pending && write_state.terminal_count != 0u) {
        status = vsock_style_sharded_consume_terminal(
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

static uint64_t vsock_style_stats_delta(uint64_t after, uint64_t before) {
  return after >= before ? after - before : 0u;
}

static void vsock_style_same_driver_run(native_io_sharded_context *context, void *arg) {
  vsock_style_same_driver *driver = (vsock_style_same_driver *)arg;
  vsock_style_sharded_fixture *fixture = driver->fixture;

  driver->status = SALTS_OK;
  for (size_t index = 0u; index < VSOCK_STYLE_WARMUP_TRANSFERS; ++index) {
    driver->status = vsock_style_sharded_transfer(fixture, context);
    if (driver->status != SALTS_OK) return;
  }

  if (!native_io_sharded_get_stats(fixture->runtime, &driver->before)) {
    driver->status = SALTS_EIO;
    return;
  }

  for (size_t index = 0u; index < VSOCK_STYLE_MEASURED_TRANSFERS; ++index) {
    const uint64_t started = salts_hrtime();
    driver->status = vsock_style_sharded_transfer(fixture, context);
    driver->latencies[index] = salts_hrtime() - started;
    if (driver->status != SALTS_OK) return;
  }

  if (!native_io_sharded_get_stats(fixture->runtime, &driver->after))
    driver->status = SALTS_EIO;
}

static int vsock_style_measure_sharded(native_io_backend_kind kind,
                                      vsock_style_kind style,
                                      size_t payload_size,
                                      vsock_style_result *out) {
  vsock_style_sharded_fixture fixture;
  uint64_t *latencies =
      (uint64_t *)calloc(VSOCK_STYLE_MEASURED_TRANSFERS, sizeof(*latencies));
  native_io_sharded_stats before = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
  native_io_sharded_stats after = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
  int status;

  if (latencies == NULL) return SALTS_ENOMEM;
  status = vsock_style_sharded_fixture_init(&fixture, kind, payload_size);
  if (status != SALTS_OK) goto cleanup;

  if (style == VSOCK_STYLE_SHARDED_SAME_OWNER) {
    vsock_style_same_driver driver = {
        &fixture, latencies,
        NATIVE_IO_SHARDED_STATS_V1_INITIALIZER,
        NATIVE_IO_SHARDED_STATS_V1_INITIALIZER,
        SALTS_OK};
    native_io_sharded_task task = {
        vsock_style_same_driver_run, NULL, NULL, &driver};
    status = native_io_sharded_submit_to(fixture.runtime, 1u, &task);
    if (status == SALTS_OK) status = native_io_sharded_wait(fixture.runtime);
    if (status == SALTS_OK) status = driver.status;
    before = driver.before;
    after = driver.after;
  } else {
    for (size_t index = 0u; index < VSOCK_STYLE_WARMUP_TRANSFERS; ++index) {
      status = vsock_style_sharded_transfer(&fixture, NULL);
      if (status != SALTS_OK) goto cleanup;
    }
    if (!native_io_sharded_get_stats(fixture.runtime, &before)) {
      status = SALTS_EIO;
      goto cleanup;
    }
    for (size_t index = 0u; index < VSOCK_STYLE_MEASURED_TRANSFERS; ++index) {
      const uint64_t started = salts_hrtime();
      status = vsock_style_sharded_transfer(&fixture, NULL);
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
    out->style = vsock_style_name(style);
    out->payload_size = payload_size;
    out->transfers = VSOCK_STYLE_MEASURED_TRANSFERS;
    for (size_t index = 0u; index < VSOCK_STYLE_MEASURED_TRANSFERS; ++index)
      out->wall_ns += latencies[index];
    out->queued_dispatches =
        vsock_style_stats_delta(after.queued_dispatches, before.queued_dispatches);
    out->rejected_tasks =
        vsock_style_stats_delta(after.rejected_tasks, before.rejected_tasks);
    {
      const uint64_t direct_tasks =
          vsock_style_stats_delta(after.same_shard_direct_tasks,
                                 before.same_shard_direct_tasks);
      out->same_owner_direct_tasks_per_transfer =
          (double)direct_tasks / (double)VSOCK_STYLE_MEASURED_TRANSFERS;
    }
    out->message_hops_per_transfer =
        (double)out->queued_dispatches /
        (double)VSOCK_STYLE_MEASURED_TRANSFERS;
    out->peak_command_slots = after.peak_command_slots;
    status = vsock_style_finalize_result(out, latencies);
  }

cleanup:
  {
    const int destroy_status = vsock_style_sharded_fixture_destroy(&fixture);
    if (status == SALTS_OK) status = destroy_status;
  }
  free(latencies);
  return status;
}

static FILE *vsock_style_open_csv(void) {
  const char *prefix = getenv("NATIVE_IO_VSOCK_STYLE_OUTPUT");
  char path[1024];
  if (prefix == NULL || *prefix == '\0') return NULL;
  if (snprintf(path, sizeof(path), "%s.csv", prefix) < 0) return NULL;
  return fopen(path, "w");
}

static void vsock_style_print_csv_row(FILE *stream, const char *backend,
                                     const vsock_style_result *result) {
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
  const native_io_backend_kind kind = vsock_style_backend();
  const char *backend = vsock_style_backend_name(kind);
  const vsock_style_kind styles[] = {
      VSOCK_STYLE_DIRECT,
      VSOCK_STYLE_COROUTINE,
      VSOCK_STYLE_SHARDED_SAME_OWNER,
      VSOCK_STYLE_SHARDED_CROSS_OWNER};
  const size_t style_count = sizeof(styles) / sizeof(styles[0]);
  const size_t payload_count =
      sizeof(VSOCK_STYLE_PAYLOADS) / sizeof(VSOCK_STYLE_PAYLOADS[0]);
  vsock_style_result results[4][4];
  FILE *csv = NULL;

  if (kind == (native_io_backend_kind)0 ||
      !native_io_backend_kind_supported(kind)) {
    fprintf(stderr, "unsupported NativeIO AF_VSOCK style backend: %s\n", backend);
    return 2;
  }

  {
    vsock_style_socket probe[2] = {
        VSOCK_STYLE_INVALID_SOCKET, VSOCK_STYLE_INVALID_SOCKET};
    const int capability = vsock_style_make_pair(probe);
    if (capability == SALTS_ENOTSUP) {
      printf("# NativeIO AF_VSOCK execution-style benchmark\n\n");
      printf("Backend: %s\n\n", backend);
      printf("Capability: unsupported on this runtime; no TCP fallback was attempted.\n");
      return 77;
    }
    if (capability != SALTS_OK) {
      fprintf(stderr, "AF_VSOCK capability probe failed: %d\n", capability);
      return 1;
    }
    vsock_style_close_socket(probe[0]);
    vsock_style_close_socket(probe[1]);
  }

  memset(results, 0, sizeof(results));
  for (size_t style_index = 0u; style_index < style_count; ++style_index) {
    for (size_t payload_index = 0u; payload_index < payload_count; ++payload_index) {
      const vsock_style_kind style = styles[style_index];
      int status;
      if (style == VSOCK_STYLE_DIRECT || style == VSOCK_STYLE_COROUTINE)
        status = vsock_style_measure_backend(kind, style,
                                            VSOCK_STYLE_PAYLOADS[payload_index],
                                            &results[style_index][payload_index]);
      else
        status = vsock_style_measure_sharded(kind, style,
                                            VSOCK_STYLE_PAYLOADS[payload_index],
                                            &results[style_index][payload_index]);
      if (status != SALTS_OK) {
        fprintf(stderr, "%s %s %zu-byte benchmark failed: %d\n",
                backend, vsock_style_name(style),
                VSOCK_STYLE_PAYLOADS[payload_index], status);
        return 1;
      }
    }
  }

  printf("# NativeIO AF_VSOCK execution-style benchmark\n\n");
  printf("Backend: %s\n\n", backend);
  printf("Each cell measures %d complete one-way transfers after %d warmups. "
         "Latency percentiles are individual complete-transfer latencies.\n\n",
         VSOCK_STYLE_MEASURED_TRANSFERS, VSOCK_STYLE_WARMUP_TRANSFERS);
  printf("| style | payload | p50 us | p95 us | MiB/s | message hops/transfer | "
         "same-owner direct tasks/transfer | queued dispatches | rejected | peak command slots |\n");
  printf("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t style_index = 0u; style_index < style_count; ++style_index) {
    for (size_t payload_index = 0u; payload_index < payload_count; ++payload_index) {
      const vsock_style_result *result = &results[style_index][payload_index];
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

  csv = vsock_style_open_csv();
  if (csv != NULL) {
    fprintf(csv,
            "backend,style,payload_bytes,transfers,p50_us,p95_us,mib_per_second,"
            "message_hops_per_transfer,same_owner_direct_tasks_per_transfer,"
            "queued_dispatches,rejected_tasks,peak_command_slots\n");
    for (size_t style_index = 0u; style_index < style_count; ++style_index)
      for (size_t payload_index = 0u; payload_index < payload_count; ++payload_index)
        vsock_style_print_csv_row(csv, backend,
                                 &results[style_index][payload_index]);
    fclose(csv);
  }

  return 0;
}
