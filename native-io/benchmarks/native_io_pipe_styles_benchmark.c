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
  #include <windows.h>
typedef HANDLE pipe_style_handle;
  #define PIPE_STYLE_INVALID_HANDLE INVALID_HANDLE_VALUE
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <unistd.h>
typedef int pipe_style_handle;
  #define PIPE_STYLE_INVALID_HANDLE (-1)
#endif

enum {
  PIPE_STYLE_WARMUP_TRANSFERS = 32,
  PIPE_STYLE_MEASURED_TRANSFERS = 512,
  PIPE_STYLE_TIMEOUT_MS = 5000,
  PIPE_STYLE_REQUEST_CAPACITY = 4,
  PIPE_STYLE_COMPLETION_CAPACITY = 4,
  PIPE_STYLE_SHARDED_QUEUE_CAPACITY = 64,
  PIPE_STYLE_PIPE_BUFFER_CAPACITY = 65536
};

static const size_t PIPE_STYLE_PAYLOADS[] = {1024u, 8192u, 32768u, 65536u};

typedef enum pipe_style_kind {
  PIPE_STYLE_DIRECT = 0,
  PIPE_STYLE_COROUTINE,
  PIPE_STYLE_SHARDED_SAME_OWNER,
  PIPE_STYLE_SHARDED_CROSS_OWNER
} pipe_style_kind;

typedef struct pipe_style_result {
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
} pipe_style_result;

typedef struct pipe_style_backend_fixture {
  native_io_backend backend;
  native_io_endpoint endpoints[2];
  pipe_style_handle handles[2];
  unsigned char *sent;
  unsigned char *received;
  size_t payload_size;
} pipe_style_backend_fixture;

typedef struct pipe_style_coroutine_operation {
  pipe_style_backend_fixture *fixture;
  unsigned char *buffer;
  size_t length;
  size_t offset;
  int status;
  bool write;
  bool done;
} pipe_style_coroutine_operation;

typedef struct pipe_style_sharded_fixture {
  native_io_sharded *runtime;
  native_io_sharded_endpoint endpoints[2];
  pipe_style_handle handles[2];
  unsigned char *sent;
  unsigned char *received;
  size_t payload_size;
  int attach_status;
  int release_status;
} pipe_style_sharded_fixture;

typedef struct pipe_style_sharded_operation {
  int admission_status;
  native_io_sharded_request request;
  native_io_completion_kind kind;
  int terminal_status;
  size_t bytes;
  uintptr_t user_data;
  unsigned terminal_count;
  unsigned finalize_count;
} pipe_style_sharded_operation;

typedef struct pipe_style_sharded_observe {
  native_io_sharded_completion events[PIPE_STYLE_COMPLETION_CAPACITY];
  size_t count;
  int status;
} pipe_style_sharded_observe;

typedef struct pipe_style_same_driver {
  pipe_style_sharded_fixture *fixture;
  uint64_t *latencies;
  native_io_sharded_stats before;
  native_io_sharded_stats after;
  int status;
} pipe_style_same_driver;

static native_io_backend_kind pipe_style_backend(void) {
  const char *value = getenv("NATIVE_IO_PIPE_STYLE_BACKEND");
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

static const char *pipe_style_backend_name(native_io_backend_kind kind) {
  switch (kind) {
    case NATIVE_IO_BACKEND_EPOLL: return "epoll";
    case NATIVE_IO_BACKEND_IO_URING: return "io_uring";
    case NATIVE_IO_BACKEND_IOCP: return "iocp";
    case NATIVE_IO_BACKEND_KQUEUE: return "kqueue";
    default: return "unsupported";
  }
}

static const char *pipe_style_name(pipe_style_kind style) {
  switch (style) {
    case PIPE_STYLE_DIRECT: return "direct";
    case PIPE_STYLE_COROUTINE: return "coroutine";
    case PIPE_STYLE_SHARDED_SAME_OWNER: return "sharded_same_owner";
    case PIPE_STYLE_SHARDED_CROSS_OWNER: return "sharded_cross_owner";
    default: return "unknown";
  }
}

#if defined(_WIN32)
static int pipe_style_native_error(DWORD error) {
  return error == ERROR_SUCCESS ? SALTS_EIO : -(int)error;
}

static void pipe_style_close_handle(pipe_style_handle handle) {
  if (handle != PIPE_STYLE_INVALID_HANDLE) (void)CloseHandle(handle);
}

static int pipe_style_make_pair(pipe_style_handle handles[2]) {
  static LONG sequence = 0;
  char name[128];
  OVERLAPPED connected = {0};
  HANDLE event = NULL;
  DWORD error = ERROR_SUCCESS;
  BOOL pending = FALSE;
  int name_length;

  handles[0] = PIPE_STYLE_INVALID_HANDLE;
  handles[1] = PIPE_STYLE_INVALID_HANDLE;
  name_length = snprintf(name, sizeof(name), "\\\\.\\pipe\\native-io-style-%lu-%ld",
                         GetCurrentProcessId(), InterlockedIncrement(&sequence));
  if (name_length < 0 || (size_t)name_length >= sizeof(name)) return SALTS_ERANGE;

  handles[1] = CreateNamedPipeA(name, PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
                                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1u,
                                PIPE_STYLE_PIPE_BUFFER_CAPACITY,
                                PIPE_STYLE_PIPE_BUFFER_CAPACITY, 0u, NULL);
  if (handles[1] == PIPE_STYLE_INVALID_HANDLE)
    return pipe_style_native_error(GetLastError());

  event = CreateEventA(NULL, TRUE, FALSE, NULL);
  if (event == NULL) {
    error = GetLastError();
    goto failed;
  }
  connected.hEvent = event;
  if (!ConnectNamedPipe(handles[1], &connected)) {
    error = GetLastError();
    if (error == ERROR_IO_PENDING)
      pending = TRUE;
    else if (error != ERROR_PIPE_CONNECTED)
      goto failed;
  }

  handles[0] =
      CreateFileA(name, GENERIC_READ, 0u, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
  if (handles[0] == PIPE_STYLE_INVALID_HANDLE) {
    error = GetLastError();
    goto failed;
  }

  if (pending) {
    DWORD transferred = 0u;
    if (!GetOverlappedResult(handles[1], &connected, &transferred, TRUE)) {
      error = GetLastError();
      goto failed;
    }
  }
  (void)CloseHandle(event);
  return SALTS_OK;

failed:
  pipe_style_close_handle(handles[0]);
  pipe_style_close_handle(handles[1]);
  if (event != NULL) (void)CloseHandle(event);
  handles[0] = PIPE_STYLE_INVALID_HANDLE;
  handles[1] = PIPE_STYLE_INVALID_HANDLE;
  return pipe_style_native_error(error);
}
#else
static void pipe_style_close_handle(pipe_style_handle handle) {
  if (handle >= 0) (void)close(handle);
}

static int pipe_style_set_nonblocking(int descriptor) {
  const int flags = fcntl(descriptor, F_GETFL, 0);
  if (flags < 0) return -errno;
  if ((flags & O_NONBLOCK) != 0) return SALTS_OK;
  return fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0 ? SALTS_OK : -errno;
}

static int pipe_style_make_pair(pipe_style_handle handles[2]) {
  int status;
  handles[0] = PIPE_STYLE_INVALID_HANDLE;
  handles[1] = PIPE_STYLE_INVALID_HANDLE;
  if (pipe(handles) != 0) return -errno;
  status = pipe_style_set_nonblocking(handles[0]);
  if (status == SALTS_OK) status = pipe_style_set_nonblocking(handles[1]);
  if (status != SALTS_OK) {
    pipe_style_close_handle(handles[0]);
    pipe_style_close_handle(handles[1]);
    handles[0] = PIPE_STYLE_INVALID_HANDLE;
    handles[1] = PIPE_STYLE_INVALID_HANDLE;
  }
  return status;
}
#endif

static int pipe_style_compare_u64(const void *left, const void *right) {
  const uint64_t a = *(const uint64_t *)left;
  const uint64_t b = *(const uint64_t *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static int pipe_style_finalize_result(pipe_style_result *result, uint64_t *latencies) {
  if (result == NULL || latencies == NULL || result->transfers == 0u) return SALTS_EINVAL;
  qsort(latencies, result->transfers, sizeof(*latencies), pipe_style_compare_u64);
  result->p50_ns = latencies[(result->transfers - 1u) * 50u / 100u];
  result->p95_ns = latencies[(result->transfers - 1u) * 95u / 100u];
  result->mib_per_second =
      result->wall_ns == 0u
          ? 0.0
          : (double)result->payload_size * (double)result->transfers * 1.0e9 /
                (double)result->wall_ns / (1024.0 * 1024.0);
  return SALTS_OK;
}

static int pipe_style_backend_fixture_init(pipe_style_backend_fixture *fixture,
                                           native_io_backend_kind kind,
                                           size_t payload_size) {
  const native_io_backend_config config = {
      kind, 2u, PIPE_STYLE_REQUEST_CAPACITY, PIPE_STYLE_COMPLETION_CAPACITY};
  int status;

  memset(fixture, 0, sizeof(*fixture));
  fixture->handles[0] = PIPE_STYLE_INVALID_HANDLE;
  fixture->handles[1] = PIPE_STYLE_INVALID_HANDLE;
  fixture->payload_size = payload_size;
  fixture->sent = (unsigned char *)malloc(payload_size);
  fixture->received = (unsigned char *)malloc(payload_size);
  if (fixture->sent == NULL || fixture->received == NULL) return SALTS_ENOMEM;
  memset(fixture->sent, 0x5au, payload_size);
  memset(fixture->received, 0, payload_size);

  status = pipe_style_make_pair(fixture->handles);
  if (status != SALTS_OK) return status;
  status = native_io_backend_init(&fixture->backend, &config);
  if (status == SALTS_OK)
    status = native_io_backend_attach_pipe(&fixture->backend, (uintptr_t)fixture->handles[0],
                                           NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
                                           &fixture->endpoints[0]);
  if (status == SALTS_OK)
    status = native_io_backend_attach_pipe(&fixture->backend, (uintptr_t)fixture->handles[1],
                                           NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
                                           &fixture->endpoints[1]);
  return status;
}

static int pipe_style_backend_fixture_destroy(pipe_style_backend_fixture *fixture) {
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
    if (fixture->handles[index] != PIPE_STYLE_INVALID_HANDLE) {
      pipe_style_close_handle(fixture->handles[index]);
      fixture->handles[index] = PIPE_STYLE_INVALID_HANDLE;
    }
  }

  if (fixture->backend.impl != NULL) {
    for (size_t index = 0u; index < 2u; ++index) {
      if (native_io_endpoint_valid(fixture->endpoints[index])) {
        const int release_status =
            native_io_backend_release_pipe(&fixture->backend, fixture->endpoints[index]);
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

static int pipe_style_direct_transfer(pipe_style_backend_fixture *fixture) {
  size_t sent_offset = 0u;
  size_t received_offset = 0u;
  bool read_pending = false;
  bool write_pending = false;

  memset(fixture->received, 0, fixture->payload_size);
  while (sent_offset < fixture->payload_size || received_offset < fixture->payload_size) {
    native_io_completion events[PIPE_STYLE_COMPLETION_CAPACITY];
    size_t count = 0u;
    int status;

    if (!read_pending && received_offset < fixture->payload_size) {
      const native_io_operation operation = {
          .kind = NATIVE_IO_OPERATION_PIPE_READ,
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
          .kind = NATIVE_IO_OPERATION_PIPE_WRITE,
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
                                       PIPE_STYLE_COMPLETION_CAPACITY,
                                       PIPE_STYLE_TIMEOUT_MS, &count);
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

static void pipe_style_coroutine_entry(native_io_coroutine *coroutine, void *arg) {
  pipe_style_coroutine_operation *state = (pipe_style_coroutine_operation *)arg;
  state->status = SALTS_OK;
  while (state->offset < state->length) {
    native_io_completion completion = {0};
    const native_io_operation operation = {
        .kind = state->write ? NATIVE_IO_OPERATION_PIPE_WRITE
                             : NATIVE_IO_OPERATION_PIPE_READ,
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

static int pipe_style_coroutine_cancel_and_drain(
    pipe_style_backend_fixture *fixture, native_io_coroutine_task task,
    pipe_style_coroutine_operation *state) {
  native_io_completion events[PIPE_STYLE_COMPLETION_CAPACITY];
  int status;

  if (state->done || !native_io_coroutine_task_valid(task)) return SALTS_OK;
  status = native_io_backend_cancel_coroutine(&fixture->backend, task);
  if (status != SALTS_OK && status != SALTS_EALREADY) return status;
  while (!state->done) {
    size_t count = 0u;
    status = native_io_backend_observe(&fixture->backend, events,
                                       PIPE_STYLE_COMPLETION_CAPACITY,
                                       PIPE_STYLE_TIMEOUT_MS, &count);
    if (status != SALTS_OK) return status;
    if (count != 0u) return SALTS_EPROTO;
  }
  return SALTS_OK;
}

static int pipe_style_coroutine_transfer(pipe_style_backend_fixture *fixture) {
  pipe_style_coroutine_operation read_state = {
      fixture, fixture->received, fixture->payload_size, 0u, SALTS_OK, false, false};
  pipe_style_coroutine_operation write_state = {
      fixture, fixture->sent, fixture->payload_size, 0u, SALTS_OK, true, false};
  native_io_coroutine_task read_task = {0};
  native_io_coroutine_task write_task = {0};
  native_io_completion events[PIPE_STYLE_COMPLETION_CAPACITY];
  int status;

  memset(fixture->received, 0, fixture->payload_size);
  status = native_io_backend_spawn_coroutine(&fixture->backend,
                                             pipe_style_coroutine_entry,
                                             &read_state, &read_task);
  if (status == SALTS_OK)
    status = native_io_backend_spawn_coroutine(&fixture->backend,
                                               pipe_style_coroutine_entry,
                                               &write_state, &write_task);

  while (status == SALTS_OK && (!read_state.done || !write_state.done)) {
    size_t count = 0u;
    status = native_io_backend_observe(&fixture->backend, events,
                                       PIPE_STYLE_COMPLETION_CAPACITY,
                                       PIPE_STYLE_TIMEOUT_MS, &count);
    if (status == SALTS_OK && count != 0u) status = SALTS_EPROTO;
  }

  if (status != SALTS_OK) {
    const int failure = status;
    const int write_drain =
        pipe_style_coroutine_cancel_and_drain(fixture, write_task, &write_state);
    const int read_drain =
        pipe_style_coroutine_cancel_and_drain(fixture, read_task, &read_state);
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

static int pipe_style_measure_backend(native_io_backend_kind kind, pipe_style_kind style,
                                      size_t payload_size, pipe_style_result *out) {
  pipe_style_backend_fixture fixture;
  uint64_t *latencies =
      (uint64_t *)calloc(PIPE_STYLE_MEASURED_TRANSFERS, sizeof(*latencies));
  int status;

  if (latencies == NULL) return SALTS_ENOMEM;
  status = pipe_style_backend_fixture_init(&fixture, kind, payload_size);
  if (status != SALTS_OK) goto cleanup;

  for (size_t index = 0u; index < PIPE_STYLE_WARMUP_TRANSFERS; ++index) {
    status = style == PIPE_STYLE_DIRECT ? pipe_style_direct_transfer(&fixture)
                                        : pipe_style_coroutine_transfer(&fixture);
    if (status != SALTS_OK) goto cleanup;
  }

  memset(out, 0, sizeof(*out));
  out->style = pipe_style_name(style);
  out->payload_size = payload_size;
  out->transfers = PIPE_STYLE_MEASURED_TRANSFERS;
  for (size_t index = 0u; index < PIPE_STYLE_MEASURED_TRANSFERS; ++index) {
    const uint64_t started = salts_hrtime();
    status = style == PIPE_STYLE_DIRECT ? pipe_style_direct_transfer(&fixture)
                                        : pipe_style_coroutine_transfer(&fixture);
    latencies[index] = salts_hrtime() - started;
    if (status != SALTS_OK) goto cleanup;
    out->wall_ns += latencies[index];
  }
  status = pipe_style_finalize_result(out, latencies);

cleanup:
  {
    const int destroy_status = pipe_style_backend_fixture_destroy(&fixture);
    if (status == SALTS_OK) status = destroy_status;
  }
  free(latencies);
  return status;
}

static void pipe_style_sharded_attach(native_io_sharded_context *context, void *arg) {
  pipe_style_sharded_fixture *fixture = (pipe_style_sharded_fixture *)arg;
  fixture->attach_status = native_io_sharded_context_attach_pipe(
      context, (uintptr_t)fixture->handles[0],
      NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &fixture->endpoints[0]);
  if (fixture->attach_status == SALTS_OK)
    fixture->attach_status = native_io_sharded_context_attach_pipe(
        context, (uintptr_t)fixture->handles[1],
        NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &fixture->endpoints[1]);
}

static void pipe_style_sharded_release(native_io_sharded_context *context, void *arg) {
  pipe_style_sharded_fixture *fixture = (pipe_style_sharded_fixture *)arg;
  int status = SALTS_OK;
  for (size_t index = 0u; index < 2u; ++index) {
    if (!native_io_sharded_endpoint_valid(fixture->endpoints[index])) continue;
    {
      const int release_status =
          native_io_sharded_context_release_pipe(context, fixture->endpoints[index]);
      if (status == SALTS_OK) status = release_status;
      fixture->endpoints[index] = (native_io_sharded_endpoint){0};
    }
  }
  fixture->release_status = status;
}

static int pipe_style_sharded_fixture_init(pipe_style_sharded_fixture *fixture,
                                           native_io_backend_kind kind,
                                           size_t payload_size) {
  const native_io_sharded_config config = {
      2u, PIPE_STYLE_SHARDED_QUEUE_CAPACITY,
      {kind, 2u, PIPE_STYLE_REQUEST_CAPACITY, PIPE_STYLE_COMPLETION_CAPACITY}};
  native_io_sharded_task attach_task;
  int status;

  memset(fixture, 0, sizeof(*fixture));
  fixture->handles[0] = PIPE_STYLE_INVALID_HANDLE;
  fixture->handles[1] = PIPE_STYLE_INVALID_HANDLE;
  fixture->payload_size = payload_size;
  fixture->sent = (unsigned char *)malloc(payload_size);
  fixture->received = (unsigned char *)malloc(payload_size);
  if (fixture->sent == NULL || fixture->received == NULL) return SALTS_ENOMEM;
  memset(fixture->sent, 0x5au, payload_size);
  memset(fixture->received, 0, payload_size);

  status = pipe_style_make_pair(fixture->handles);
  if (status != SALTS_OK) return status;
  status = native_io_sharded_create(&config, &fixture->runtime);
  if (status != SALTS_OK) return status;

  attach_task =
      (native_io_sharded_task){pipe_style_sharded_attach, NULL, NULL, fixture};
  status = native_io_sharded_submit_to(fixture->runtime, 1u, &attach_task);
  if (status == SALTS_OK) status = native_io_sharded_wait(fixture->runtime);
  if (status == SALTS_OK) status = fixture->attach_status;
  return status;
}

static int pipe_style_sharded_fixture_destroy(pipe_style_sharded_fixture *fixture) {
  int status = SALTS_OK;

  for (size_t index = 0u; index < 2u; ++index) {
    if (fixture->handles[index] != PIPE_STYLE_INVALID_HANDLE) {
      pipe_style_close_handle(fixture->handles[index]);
      fixture->handles[index] = PIPE_STYLE_INVALID_HANDLE;
    }
  }

  if (fixture->runtime != NULL) {
    native_io_sharded_task release_task = {
        pipe_style_sharded_release, NULL, NULL, fixture};
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

static void pipe_style_sharded_admission(native_io_sharded_context *context, int status,
                                         native_io_sharded_request request, void *arg) {
  pipe_style_sharded_operation *state = (pipe_style_sharded_operation *)arg;
  (void)context;
  state->admission_status = status;
  state->request = request;
}

static void pipe_style_sharded_terminal(
    native_io_sharded_context *context,
    const native_io_sharded_completion *completion, void *arg) {
  pipe_style_sharded_operation *state = (pipe_style_sharded_operation *)arg;
  (void)context;
  state->kind = completion->kind;
  state->terminal_status = completion->status;
  state->bytes = completion->bytes;
  state->user_data = completion->user_data;
  ++state->terminal_count;
}

static void pipe_style_sharded_finalize(void *arg) {
  pipe_style_sharded_operation *state = (pipe_style_sharded_operation *)arg;
  ++state->finalize_count;
}

static void pipe_style_sharded_reset_operation(pipe_style_sharded_operation *state) {
  memset(state, 0, sizeof(*state));
  state->admission_status = SALTS_EIO;
  state->terminal_status = SALTS_EIO;
}

static int pipe_style_sharded_submit_operation(
    pipe_style_sharded_fixture *fixture, size_t endpoint_index,
    native_io_operation_kind kind, unsigned char *buffer, size_t length,
    uintptr_t user_data, pipe_style_sharded_operation *state) {
  const native_io_sharded_operation operation = {
      .kind = kind,
      .endpoint = fixture->endpoints[endpoint_index],
      .buffer = buffer,
      .length = length,
      .user_data = user_data};
  const native_io_sharded_ownership ownership = {
      pipe_style_sharded_terminal, pipe_style_sharded_finalize, state};
  pipe_style_sharded_reset_operation(state);
  return native_io_sharded_submit_owned(
      fixture->runtime, &operation, &ownership,
      pipe_style_sharded_admission, state);
}

static int pipe_style_sharded_consume_terminal(
    pipe_style_sharded_operation *state, size_t remaining, size_t *offset) {
  if (state->terminal_count == 0u) return SALTS_ETIMEDOUT;
  if (state->terminal_count != 1u || state->finalize_count != 1u)
    return SALTS_EPROTO;
  if (state->kind != NATIVE_IO_COMPLETION_OK || state->bytes == 0u ||
      state->bytes > remaining)
    return state->terminal_status != SALTS_OK ? state->terminal_status : SALTS_EIO;
  *offset += state->bytes;
  return SALTS_OK;
}

static void pipe_style_sharded_observe_task(native_io_sharded_context *context, void *arg) {
  pipe_style_sharded_observe *observe = (pipe_style_sharded_observe *)arg;
  observe->count = 0u;
  observe->status = native_io_sharded_context_observe(
      context, observe->events, PIPE_STYLE_COMPLETION_CAPACITY,
      PIPE_STYLE_TIMEOUT_MS, &observe->count);
}

static int pipe_style_sharded_transfer(
    pipe_style_sharded_fixture *fixture, native_io_sharded_context *same_owner_context) {
  size_t read_offset = 0u;
  size_t write_offset = 0u;

  memset(fixture->received, 0, fixture->payload_size);
  while (read_offset < fixture->payload_size ||
         write_offset < fixture->payload_size) {
    pipe_style_sharded_operation read_state;
    pipe_style_sharded_operation write_state;
    bool read_pending = false;
    bool write_pending = false;
    int status = SALTS_OK;

    if (read_offset < fixture->payload_size) {
      status = pipe_style_sharded_submit_operation(
          fixture, 0u, NATIVE_IO_OPERATION_PIPE_READ,
          fixture->received + read_offset,
          fixture->payload_size - read_offset, 1u, &read_state);
      if (status != SALTS_OK) return status;
      read_pending = true;
    }
    if (write_offset < fixture->payload_size) {
      status = pipe_style_sharded_submit_operation(
          fixture, 1u, NATIVE_IO_OPERATION_PIPE_WRITE,
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
      pipe_style_sharded_observe observe = {0};
      if (same_owner_context != NULL) {
        pipe_style_sharded_observe_task(same_owner_context, &observe);
      } else {
        native_io_sharded_task observe_task = {
            pipe_style_sharded_observe_task, NULL, NULL, &observe};
        status = native_io_sharded_submit_to(fixture->runtime, 1u, &observe_task);
        if (status == SALTS_OK) status = native_io_sharded_wait(fixture->runtime);
        if (status != SALTS_OK) return status;
      }

      if (observe.status != SALTS_OK) return observe.status;
      if (observe.count == 0u) return SALTS_EIO;

      if (read_pending && read_state.terminal_count != 0u) {
        status = pipe_style_sharded_consume_terminal(
            &read_state, fixture->payload_size - read_offset, &read_offset);
        if (status != SALTS_OK) return status;
        read_pending = false;
      }
      if (write_pending && write_state.terminal_count != 0u) {
        status = pipe_style_sharded_consume_terminal(
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

static uint64_t pipe_style_stats_delta(uint64_t after, uint64_t before) {
  return after >= before ? after - before : 0u;
}

static void pipe_style_same_driver_run(native_io_sharded_context *context, void *arg) {
  pipe_style_same_driver *driver = (pipe_style_same_driver *)arg;
  pipe_style_sharded_fixture *fixture = driver->fixture;

  driver->status = SALTS_OK;
  for (size_t index = 0u; index < PIPE_STYLE_WARMUP_TRANSFERS; ++index) {
    driver->status = pipe_style_sharded_transfer(fixture, context);
    if (driver->status != SALTS_OK) return;
  }

  if (!native_io_sharded_get_stats(fixture->runtime, &driver->before)) {
    driver->status = SALTS_EIO;
    return;
  }

  for (size_t index = 0u; index < PIPE_STYLE_MEASURED_TRANSFERS; ++index) {
    const uint64_t started = salts_hrtime();
    driver->status = pipe_style_sharded_transfer(fixture, context);
    driver->latencies[index] = salts_hrtime() - started;
    if (driver->status != SALTS_OK) return;
  }

  if (!native_io_sharded_get_stats(fixture->runtime, &driver->after))
    driver->status = SALTS_EIO;
}

static int pipe_style_measure_sharded(native_io_backend_kind kind,
                                      pipe_style_kind style,
                                      size_t payload_size,
                                      pipe_style_result *out) {
  pipe_style_sharded_fixture fixture;
  uint64_t *latencies =
      (uint64_t *)calloc(PIPE_STYLE_MEASURED_TRANSFERS, sizeof(*latencies));
  native_io_sharded_stats before = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
  native_io_sharded_stats after = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
  int status;

  if (latencies == NULL) return SALTS_ENOMEM;
  status = pipe_style_sharded_fixture_init(&fixture, kind, payload_size);
  if (status != SALTS_OK) goto cleanup;

  if (style == PIPE_STYLE_SHARDED_SAME_OWNER) {
    pipe_style_same_driver driver = {
        &fixture, latencies,
        NATIVE_IO_SHARDED_STATS_V1_INITIALIZER,
        NATIVE_IO_SHARDED_STATS_V1_INITIALIZER,
        SALTS_OK};
    native_io_sharded_task task = {
        pipe_style_same_driver_run, NULL, NULL, &driver};
    status = native_io_sharded_submit_to(fixture.runtime, 1u, &task);
    if (status == SALTS_OK) status = native_io_sharded_wait(fixture.runtime);
    if (status == SALTS_OK) status = driver.status;
    before = driver.before;
    after = driver.after;
  } else {
    for (size_t index = 0u; index < PIPE_STYLE_WARMUP_TRANSFERS; ++index) {
      status = pipe_style_sharded_transfer(&fixture, NULL);
      if (status != SALTS_OK) goto cleanup;
    }
    if (!native_io_sharded_get_stats(fixture.runtime, &before)) {
      status = SALTS_EIO;
      goto cleanup;
    }
    for (size_t index = 0u; index < PIPE_STYLE_MEASURED_TRANSFERS; ++index) {
      const uint64_t started = salts_hrtime();
      status = pipe_style_sharded_transfer(&fixture, NULL);
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
    out->style = pipe_style_name(style);
    out->payload_size = payload_size;
    out->transfers = PIPE_STYLE_MEASURED_TRANSFERS;
    for (size_t index = 0u; index < PIPE_STYLE_MEASURED_TRANSFERS; ++index)
      out->wall_ns += latencies[index];
    out->queued_dispatches =
        pipe_style_stats_delta(after.queued_dispatches, before.queued_dispatches);
    out->rejected_tasks =
        pipe_style_stats_delta(after.rejected_tasks, before.rejected_tasks);
    {
      const uint64_t direct_tasks =
          pipe_style_stats_delta(after.same_shard_direct_tasks,
                                 before.same_shard_direct_tasks);
      out->same_owner_direct_tasks_per_transfer =
          (double)direct_tasks / (double)PIPE_STYLE_MEASURED_TRANSFERS;
    }
    out->message_hops_per_transfer =
        (double)out->queued_dispatches /
        (double)PIPE_STYLE_MEASURED_TRANSFERS;
    out->peak_command_slots = after.peak_command_slots;
    status = pipe_style_finalize_result(out, latencies);
  }

cleanup:
  {
    const int destroy_status = pipe_style_sharded_fixture_destroy(&fixture);
    if (status == SALTS_OK) status = destroy_status;
  }
  free(latencies);
  return status;
}

static FILE *pipe_style_open_csv(void) {
  const char *prefix = getenv("NATIVE_IO_PIPE_STYLE_OUTPUT");
  char path[1024];
  if (prefix == NULL || *prefix == '\0') return NULL;
  if (snprintf(path, sizeof(path), "%s.csv", prefix) < 0) return NULL;
  return fopen(path, "w");
}

static void pipe_style_print_csv_row(FILE *stream, const char *backend,
                                     const pipe_style_result *result) {
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
  const native_io_backend_kind kind = pipe_style_backend();
  const char *backend = pipe_style_backend_name(kind);
  const pipe_style_kind styles[] = {
      PIPE_STYLE_DIRECT,
      PIPE_STYLE_COROUTINE,
      PIPE_STYLE_SHARDED_SAME_OWNER,
      PIPE_STYLE_SHARDED_CROSS_OWNER};
  const size_t style_count = sizeof(styles) / sizeof(styles[0]);
  const size_t payload_count =
      sizeof(PIPE_STYLE_PAYLOADS) / sizeof(PIPE_STYLE_PAYLOADS[0]);
  pipe_style_result results[4][4];
  FILE *csv = NULL;

  if (kind == (native_io_backend_kind)0 ||
      !native_io_backend_kind_supported(kind) ||
      !native_io_backend_kind_supports_pipe(kind)) {
    fprintf(stderr, "unsupported NativeIO PIPE style backend: %s\n", backend);
    return 2;
  }

  memset(results, 0, sizeof(results));
  for (size_t style_index = 0u; style_index < style_count; ++style_index) {
    for (size_t payload_index = 0u; payload_index < payload_count; ++payload_index) {
      const pipe_style_kind style = styles[style_index];
      int status;
      if (style == PIPE_STYLE_DIRECT || style == PIPE_STYLE_COROUTINE)
        status = pipe_style_measure_backend(kind, style,
                                            PIPE_STYLE_PAYLOADS[payload_index],
                                            &results[style_index][payload_index]);
      else
        status = pipe_style_measure_sharded(kind, style,
                                            PIPE_STYLE_PAYLOADS[payload_index],
                                            &results[style_index][payload_index]);
      if (status != SALTS_OK) {
        fprintf(stderr, "%s %s %zu-byte benchmark failed: %d\n",
                backend, pipe_style_name(style),
                PIPE_STYLE_PAYLOADS[payload_index], status);
        return 1;
      }
    }
  }

  printf("# NativeIO PIPE execution-style benchmark\n\n");
  printf("Backend: %s\n\n", backend);
  printf("Each cell measures %d complete one-way transfers after %d warmups. "
         "Latency percentiles are individual complete-transfer latencies.\n\n",
         PIPE_STYLE_MEASURED_TRANSFERS, PIPE_STYLE_WARMUP_TRANSFERS);
  printf("| style | payload | p50 us | p95 us | MiB/s | message hops/transfer | "
         "same-owner direct tasks/transfer | queued dispatches | rejected | peak command slots |\n");
  printf("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t style_index = 0u; style_index < style_count; ++style_index) {
    for (size_t payload_index = 0u; payload_index < payload_count; ++payload_index) {
      const pipe_style_result *result = &results[style_index][payload_index];
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

  csv = pipe_style_open_csv();
  if (csv != NULL) {
    fprintf(csv,
            "backend,style,payload_bytes,transfers,p50_us,p95_us,mib_per_second,"
            "message_hops_per_transfer,same_owner_direct_tasks_per_transfer,"
            "queued_dispatches,rejected_tasks,peak_command_slots\n");
    for (size_t style_index = 0u; style_index < style_count; ++style_index)
      for (size_t payload_index = 0u; payload_index < payload_count; ++payload_index)
        pipe_style_print_csv_row(csv, backend,
                                 &results[style_index][payload_index]);
    fclose(csv);
  }

  return 0;
}
