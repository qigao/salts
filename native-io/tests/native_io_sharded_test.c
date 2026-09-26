#include "tinytest.h"

#include <salts/error_codes.h>
#include <salts/native_io_sharded.h>
#include <salts/thread.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <unistd.h>
#endif

enum {
  NATIVE_IO_SHARDED_TEST_WAIT_ROUNDS = 2000,
  NATIVE_IO_SHARDED_TEST_PIPE_BUFFER_CAPACITY = 4096
};

static native_io_backend_kind native_io_sharded_test_backend(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
  return NATIVE_IO_BACKEND_EPOLL;
#elif (defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
       defined(__DragonFly__)) && \
    UINTPTR_MAX > UINT32_MAX
  return NATIVE_IO_BACKEND_KQUEUE;
#else
  return (native_io_backend_kind)0;
#endif
}

static int native_io_sharded_test_create_kind(native_io_backend_kind kind,
                                              size_t shards, size_t queue_capacity,
                                              native_io_sharded **out_runtime) {
  native_io_sharded_config config = {
      shards, queue_capacity, {kind, 2u, 2u, 2u}};
  if (kind == (native_io_backend_kind)0 || !native_io_backend_kind_supported(kind))
    return SALTS_ENOTSUP;
  return native_io_sharded_create(&config, out_runtime);
}

static int native_io_sharded_test_create(size_t shards, size_t queue_capacity,
                                         native_io_sharded **out_runtime) {
  return native_io_sharded_test_create_kind(
      native_io_sharded_test_backend(), shards, queue_capacity, out_runtime);
}

static int native_io_sharded_wait_atomic(atomic_int *value, int expected) {
  for (int round = 0; round < NATIVE_IO_SHARDED_TEST_WAIT_ROUNDS; ++round) {
    if (atomic_load_explicit(value, memory_order_acquire) >= expected) return 1;
    salts_sleep_ms(1u);
  }
  return 0;
}

typedef struct native_io_sharded_test_pipe {
  uintptr_t handle;
  uintptr_t peer;
} native_io_sharded_test_pipe;

static int native_io_sharded_test_pipe_create(native_io_sharded_test_pipe *pipe_endpoint) {
  if (pipe_endpoint == NULL) return SALTS_EINVAL;
#if defined(_WIN32)
  static LONG sequence = 0;
  char name[128];
  OVERLAPPED connected = {0};
  HANDLE event = NULL;
  HANDLE server = INVALID_HANDLE_VALUE;
  HANDLE client = INVALID_HANDLE_VALUE;
  DWORD error = ERROR_SUCCESS;
  BOOL pending = FALSE;
  const int length =
      snprintf(name, sizeof(name), "\\\\.\\pipe\\native-io-sharded-%lu-%ld",
               GetCurrentProcessId(), InterlockedIncrement(&sequence));
  if (length < 0 || (size_t)length >= sizeof(name)) return SALTS_ERANGE;
  server = CreateNamedPipeA(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1u,
                            NATIVE_IO_SHARDED_TEST_PIPE_BUFFER_CAPACITY,
                            NATIVE_IO_SHARDED_TEST_PIPE_BUFFER_CAPACITY, 0u, NULL);
  if (server == INVALID_HANDLE_VALUE) return -(int)GetLastError();
  event = CreateEventA(NULL, TRUE, FALSE, NULL);
  if (event == NULL) {
    error = GetLastError();
    goto failed;
  }
  connected.hEvent = event;
  if (!ConnectNamedPipe(server, &connected)) {
    error = GetLastError();
    if (error == ERROR_IO_PENDING)
      pending = TRUE;
    else if (error != ERROR_PIPE_CONNECTED)
      goto failed;
  }
  client = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0u, NULL, OPEN_EXISTING, 0u, NULL);
  if (client == INVALID_HANDLE_VALUE) {
    error = GetLastError();
    goto failed;
  }
  if (pending) {
    DWORD transferred = 0u;
    if (!GetOverlappedResult(server, &connected, &transferred, TRUE)) {
      error = GetLastError();
      goto failed;
    }
  }
  (void)CloseHandle(event);
  pipe_endpoint->handle = (uintptr_t)server;
  pipe_endpoint->peer = (uintptr_t)client;
  return SALTS_OK;

failed:
  if (client != INVALID_HANDLE_VALUE) (void)CloseHandle(client);
  if (server != INVALID_HANDLE_VALUE) (void)CloseHandle(server);
  if (event != NULL) (void)CloseHandle(event);
  return -(int)error;
#else
  int descriptors[2] = {-1, -1};
  int flags;
  if (pipe(descriptors) != 0) return -errno;
  flags = fcntl(descriptors[0], F_GETFL, 0);
  if (flags < 0 || fcntl(descriptors[0], F_SETFL, flags | O_NONBLOCK) != 0) {
    const int error = errno;
    (void)close(descriptors[0]);
    (void)close(descriptors[1]);
    return -error;
  }
  pipe_endpoint->handle = (uintptr_t)descriptors[0];
  pipe_endpoint->peer = (uintptr_t)descriptors[1];
  return SALTS_OK;
#endif
}

static void native_io_sharded_test_pipe_close_handle(uintptr_t *handle) {
  if (handle == NULL || *handle == 0u) return;
#if defined(_WIN32)
  (void)CloseHandle((HANDLE)*handle);
#else
  (void)close((int)*handle);
#endif
  *handle = 0u;
}

static void native_io_sharded_test_pipe_close(native_io_sharded_test_pipe *pipe_endpoint) {
  if (pipe_endpoint == NULL) return;
  native_io_sharded_test_pipe_close_handle(&pipe_endpoint->handle);
  native_io_sharded_test_pipe_close_handle(&pipe_endpoint->peer);
}

static int native_io_sharded_test_pipe_write(uintptr_t peer,
                                              const void *buffer, size_t length) {
  if (peer == 0u || buffer == NULL || length == 0u) return SALTS_EINVAL;
#if defined(_WIN32)
  DWORD transferred = 0u;
  if (!WriteFile((HANDLE)peer, buffer, (DWORD)length, &transferred, NULL))
    return -(int)GetLastError();
  return transferred == (DWORD)length ? SALTS_OK : SALTS_EIO;
#else
  const ssize_t transferred = write((int)peer, buffer, length);
  return transferred == (ssize_t)length ? SALTS_OK : transferred < 0 ? -errno : SALTS_EIO;
#endif
}

typedef struct native_io_sharded_direct_state {
  native_io_sharded *runtime;
  size_t expected_shard;
  atomic_int sequence;
  atomic_int migrations;
  atomic_int nested_finalizes;
  atomic_int outer_finalizes;
  int outer_sequence;
  int nested_sequence;
  int after_nested_sequence;
  int nested_status;
} native_io_sharded_direct_state;

static void native_io_sharded_nested_run(native_io_sharded_context *context, void *arg) {
  native_io_sharded_direct_state *state = (native_io_sharded_direct_state *)arg;
  if (native_io_sharded_context_shard(context) != state->expected_shard ||
      native_io_sharded_current_shard(state->runtime) != state->expected_shard)
    atomic_fetch_add(&state->migrations, 1);
  state->nested_sequence = atomic_fetch_add(&state->sequence, 1) + 1;
}

static void native_io_sharded_nested_finalize(void *arg) {
  native_io_sharded_direct_state *state = (native_io_sharded_direct_state *)arg;
  atomic_fetch_add(&state->nested_finalizes, 1);
}

static void native_io_sharded_outer_run(native_io_sharded_context *context, void *arg) {
  native_io_sharded_direct_state *state = (native_io_sharded_direct_state *)arg;
  native_io_sharded_task nested = {
      native_io_sharded_nested_run, NULL, native_io_sharded_nested_finalize, state};

  if (native_io_sharded_context_shard(context) != state->expected_shard ||
      native_io_sharded_current_shard(state->runtime) != state->expected_shard)
    atomic_fetch_add(&state->migrations, 1);
  state->outer_sequence = atomic_fetch_add(&state->sequence, 1) + 1;
  state->nested_status =
      native_io_sharded_try_submit_to(state->runtime, state->expected_shard, &nested);
  state->after_nested_sequence = atomic_fetch_add(&state->sequence, 1) + 1;
}

static void native_io_sharded_outer_finalize(void *arg) {
  native_io_sharded_direct_state *state = (native_io_sharded_direct_state *)arg;
  atomic_fetch_add(&state->outer_finalizes, 1);
}

typedef struct native_io_sharded_gate_state {
  atomic_int gate;
  atomic_int started;
  atomic_int runs;
  atomic_int cancels;
  atomic_int finalizes;
} native_io_sharded_gate_state;

typedef struct native_io_sharded_blocking_submit_state {
  native_io_sharded *runtime;
  native_io_sharded_task task;
  size_t shard;
  atomic_int entered;
  atomic_int returned;
  int status;
} native_io_sharded_blocking_submit_state;

typedef struct native_io_sharded_shutdown_state {
  native_io_sharded *runtime;
  atomic_int entered;
  atomic_int returned;
  int status;
} native_io_sharded_shutdown_state;

typedef struct native_io_sharded_affinity_state {
  native_io_sharded_endpoint endpoint;
  native_io_sharded_request request;
  native_io_sharded_completion completion;
  uintptr_t native_handle;
  uintptr_t peer;
  unsigned char byte;
  int attach_status;
  int owner_submit_status;
  int peer_write_status;
  int owner_observe_status;
  size_t owner_observe_count;
  int wrong_submit_status;
  int wrong_cancel_status;
  int wrong_release_status;
  int owner_cancel_status;
  int owner_release_status;
  int stale_submit_status;
  size_t observed_owner_shard;
  size_t observed_request_shard;
} native_io_sharded_affinity_state;

typedef struct native_io_sharded_owned_state {
  native_io_sharded_endpoint endpoint;
  native_io_sharded_request request;
  native_io_sharded_completion completion;
  uintptr_t native_handle;
  uintptr_t peer;
  unsigned char byte;
  atomic_int terminals;
  atomic_int finalizes;
  int terminal_finalizes_seen;
  int nested_observe_status;
  native_io_completion_kind terminal_kind;
  size_t terminal_bytes;
  unsigned char terminal_byte;
  size_t terminal_shard;
  int attach_status;
  int wrong_submit_status;
  int invalid_owner_status;
  int submit_status;
  int prepare_status;
  int flush_status;
  int cancel_status;
  int observe_status;
  size_t observe_count;
  int peer_write_status;
  int release_status;
} native_io_sharded_owned_state;

typedef struct native_io_sharded_route_state {
  native_io_sharded_endpoint endpoint;
  native_io_sharded_request request;
  native_io_sharded_completion completion;
  uintptr_t native_handle;
  uintptr_t peer;
  unsigned char byte;
  atomic_int admissions;
  atomic_int terminals;
  atomic_int finalizes;
  int admission_status;
  int admission_finalizes_seen;
  int terminal_resubmit_status;
  int attempt_terminal_resubmit;
  size_t admission_shard;
  native_io_completion_kind terminal_kind;
  size_t terminal_bytes;
  unsigned char terminal_byte;
  size_t terminal_shard;
  int attach_status;
  int observe_status;
  size_t observe_count;
  int peer_write_status;
  int release_status;
} native_io_sharded_route_state;

static void native_io_sharded_gate_run(native_io_sharded_context *context, void *arg) {
  native_io_sharded_gate_state *state = (native_io_sharded_gate_state *)arg;
  (void)context;
  atomic_fetch_add_explicit(&state->started, 1, memory_order_release);
  while (!atomic_load_explicit(&state->gate, memory_order_acquire))
    salts_sleep_ms(1u);
  atomic_fetch_add(&state->runs, 1);
}

static void native_io_sharded_gate_cancel(void *arg, int status) {
  native_io_sharded_gate_state *state = (native_io_sharded_gate_state *)arg;
  (void)status;
  atomic_fetch_add(&state->cancels, 1);
}

static void native_io_sharded_gate_finalize(void *arg) {
  native_io_sharded_gate_state *state = (native_io_sharded_gate_state *)arg;
  atomic_fetch_add(&state->finalizes, 1);
}

static void native_io_sharded_noop_run(native_io_sharded_context *context, void *arg) {
  atomic_int *runs = (atomic_int *)arg;
  (void)context;
  atomic_fetch_add(runs, 1);
}

static void native_io_sharded_blocking_submit_thread(void *arg) {
  native_io_sharded_blocking_submit_state *state =
      (native_io_sharded_blocking_submit_state *)arg;
  atomic_store_explicit(&state->entered, 1, memory_order_release);
  state->status = native_io_sharded_submit_to(state->runtime, state->shard, &state->task);
  atomic_store_explicit(&state->returned, 1, memory_order_release);
}

static void native_io_sharded_shutdown_thread(void *arg) {
  native_io_sharded_shutdown_state *state = (native_io_sharded_shutdown_state *)arg;
  atomic_store_explicit(&state->entered, 1, memory_order_release);
  state->status = native_io_sharded_shutdown(state->runtime);
  atomic_store_explicit(&state->returned, 1, memory_order_release);
}

static native_io_sharded_operation
native_io_sharded_affinity_read_operation(native_io_sharded_affinity_state *state) {
  native_io_sharded_operation operation = {0};
  operation.kind = NATIVE_IO_OPERATION_PIPE_READ;
  operation.endpoint = state->endpoint;
  operation.buffer = &state->byte;
  operation.length = sizeof(state->byte);
  operation.user_data = 0x475u;
  return operation;
}

static void native_io_sharded_affinity_attach(native_io_sharded_context *context, void *arg) {
  native_io_sharded_affinity_state *state = (native_io_sharded_affinity_state *)arg;
  state->attach_status = native_io_sharded_context_attach_pipe(
      context, state->native_handle, NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &state->endpoint);
  state->observed_owner_shard = native_io_sharded_endpoint_owner_shard(state->endpoint);
}

static void native_io_sharded_affinity_submit(native_io_sharded_context *context, void *arg) {
  native_io_sharded_affinity_state *state = (native_io_sharded_affinity_state *)arg;
  native_io_sharded_operation operation = native_io_sharded_affinity_read_operation(state);
  state->owner_submit_status =
      native_io_sharded_context_submit(context, &operation, &state->request);
  state->observed_request_shard = native_io_sharded_request_owner_shard(state->request);
}

static void native_io_sharded_affinity_wrong_owner(native_io_sharded_context *context, void *arg) {
  native_io_sharded_affinity_state *state = (native_io_sharded_affinity_state *)arg;
  native_io_sharded_operation operation = native_io_sharded_affinity_read_operation(state);
  native_io_sharded_request rejected = {0};
  state->wrong_submit_status =
      native_io_sharded_context_submit(context, &operation, &rejected);
  state->wrong_cancel_status =
      native_io_sharded_context_cancel(context, state->request);
  state->wrong_release_status =
      native_io_sharded_context_release_pipe(context, state->endpoint);
}

static void native_io_sharded_affinity_observe(native_io_sharded_context *context, void *arg) {
  native_io_sharded_affinity_state *state = (native_io_sharded_affinity_state *)arg;
  state->owner_observe_status =
      native_io_sharded_context_observe(context, &state->completion, 1u, 5000u,
                                        &state->owner_observe_count);
}

static void native_io_sharded_affinity_cancel(native_io_sharded_context *context, void *arg) {
  native_io_sharded_affinity_state *state = (native_io_sharded_affinity_state *)arg;
  state->owner_cancel_status = native_io_sharded_context_cancel(context, state->request);
}

static void native_io_sharded_affinity_release(native_io_sharded_context *context, void *arg) {
  native_io_sharded_affinity_state *state = (native_io_sharded_affinity_state *)arg;
  native_io_sharded_operation operation = native_io_sharded_affinity_read_operation(state);
  native_io_sharded_request request = {0};
  state->owner_release_status =
      native_io_sharded_context_release_pipe(context, state->endpoint);
  state->stale_submit_status =
      native_io_sharded_context_submit(context, &operation, &request);
}

static native_io_sharded_operation
native_io_sharded_owned_read_operation(native_io_sharded_owned_state *state) {
  native_io_sharded_operation operation = {0};
  operation.kind = NATIVE_IO_OPERATION_PIPE_READ;
  operation.endpoint = state->endpoint;
  operation.buffer = &state->byte;
  operation.length = sizeof(state->byte);
  operation.user_data = 0x4753u;
  return operation;
}

static void native_io_sharded_owned_terminal(native_io_sharded_context *context,
                                             const native_io_sharded_completion *completion,
                                             void *arg) {
  native_io_sharded_owned_state *state = (native_io_sharded_owned_state *)arg;
  native_io_sharded_completion nested_event = {0};
  size_t nested_count = 0u;
  state->terminal_finalizes_seen = atomic_load(&state->finalizes);
  state->nested_observe_status =
      native_io_sharded_context_observe(context, &nested_event, 1u, 0u, &nested_count);
  state->terminal_kind = completion->kind;
  state->terminal_bytes = completion->bytes;
  state->terminal_byte = state->byte;
  state->terminal_shard = native_io_sharded_context_shard(context);
  atomic_fetch_add(&state->terminals, 1);
}

static void native_io_sharded_owned_finalize(void *arg) {
  native_io_sharded_owned_state *state = (native_io_sharded_owned_state *)arg;
  atomic_fetch_add(&state->finalizes, 1);
}

static void native_io_sharded_owned_attach(native_io_sharded_context *context, void *arg) {
  native_io_sharded_owned_state *state = (native_io_sharded_owned_state *)arg;
  state->attach_status = native_io_sharded_context_attach_pipe(
      context, state->native_handle, NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &state->endpoint);
}

static void native_io_sharded_owned_wrong_submit(native_io_sharded_context *context, void *arg) {
  native_io_sharded_owned_state *state = (native_io_sharded_owned_state *)arg;
  native_io_sharded_operation operation = native_io_sharded_owned_read_operation(state);
  native_io_sharded_ownership ownership = {
      native_io_sharded_owned_terminal, native_io_sharded_owned_finalize, state};
  native_io_sharded_request request = {0};
  state->wrong_submit_status =
      native_io_sharded_context_submit_owned(context, &operation, &ownership, &request);
}

static void native_io_sharded_owned_submit(native_io_sharded_context *context, void *arg) {
  native_io_sharded_owned_state *state = (native_io_sharded_owned_state *)arg;
  native_io_sharded_operation operation = native_io_sharded_owned_read_operation(state);
  native_io_sharded_ownership ownership = {
      native_io_sharded_owned_terminal, native_io_sharded_owned_finalize, state};
  native_io_sharded_ownership invalid = {
      native_io_sharded_owned_terminal, NULL, state};
  native_io_sharded_request invalid_request = {0};
  state->invalid_owner_status =
      native_io_sharded_context_submit_owned(context, &operation, &invalid, &invalid_request);
  state->submit_status =
      native_io_sharded_context_submit_owned(context, &operation, &ownership, &state->request);
}

static void native_io_sharded_owned_prepare(native_io_sharded_context *context, void *arg) {
  native_io_sharded_owned_state *state = (native_io_sharded_owned_state *)arg;
  native_io_sharded_operation operation = native_io_sharded_owned_read_operation(state);
  native_io_sharded_ownership ownership = {
      native_io_sharded_owned_terminal, native_io_sharded_owned_finalize, state};
  state->prepare_status =
      native_io_sharded_context_prepare_owned(context, &operation, &ownership, &state->request);
  state->flush_status = state->prepare_status == SALTS_OK
                            ? native_io_sharded_context_flush(context)
                            : state->prepare_status;
}

static void native_io_sharded_owned_prepare_only(native_io_sharded_context *context, void *arg) {
  native_io_sharded_owned_state *state = (native_io_sharded_owned_state *)arg;
  native_io_sharded_operation operation = native_io_sharded_owned_read_operation(state);
  native_io_sharded_ownership ownership = {
      native_io_sharded_owned_terminal, native_io_sharded_owned_finalize, state};
  state->prepare_status =
      native_io_sharded_context_prepare_owned(context, &operation, &ownership, &state->request);
}

static void native_io_sharded_owned_cancel(native_io_sharded_context *context, void *arg) {
  native_io_sharded_owned_state *state = (native_io_sharded_owned_state *)arg;
  state->cancel_status = native_io_sharded_context_cancel(context, state->request);
}

static void native_io_sharded_owned_observe(native_io_sharded_context *context, void *arg) {
  native_io_sharded_owned_state *state = (native_io_sharded_owned_state *)arg;
  state->observe_count = 0u;
  state->observe_status =
      native_io_sharded_context_observe(context, &state->completion, 1u, 5000u,
                                        &state->observe_count);
}

static void native_io_sharded_owned_release(native_io_sharded_context *context, void *arg) {
  native_io_sharded_owned_state *state = (native_io_sharded_owned_state *)arg;
  state->release_status = native_io_sharded_context_release_pipe(context, state->endpoint);
}

static native_io_sharded_operation
native_io_sharded_route_read_operation(native_io_sharded_route_state *state) {
  native_io_sharded_operation operation = {0};
  operation.kind = NATIVE_IO_OPERATION_PIPE_READ;
  operation.endpoint = state->endpoint;
  operation.buffer = &state->byte;
  operation.length = sizeof(state->byte);
  operation.user_data = 0x4743u;
  return operation;
}

static void native_io_sharded_route_terminal(native_io_sharded_context *context,
                                             const native_io_sharded_completion *completion,
                                             void *arg) {
  native_io_sharded_route_state *state = (native_io_sharded_route_state *)arg;
  if (state->attempt_terminal_resubmit) {
    native_io_sharded_operation operation = native_io_sharded_route_read_operation(state);
    native_io_sharded_request rejected = {0};
    state->terminal_resubmit_status =
        native_io_sharded_context_submit(context, &operation, &rejected);
  }
  state->terminal_kind = completion->kind;
  state->terminal_bytes = completion->bytes;
  state->terminal_byte = state->byte;
  state->terminal_shard = native_io_sharded_context_shard(context);
  atomic_fetch_add(&state->terminals, 1);
}

static void native_io_sharded_route_finalize(void *arg) {
  native_io_sharded_route_state *state = (native_io_sharded_route_state *)arg;
  atomic_fetch_add(&state->finalizes, 1);
}

static void native_io_sharded_route_admission(native_io_sharded_context *context, int status,
                                              native_io_sharded_request request, void *arg) {
  native_io_sharded_route_state *state = (native_io_sharded_route_state *)arg;
  state->admission_status = status;
  state->request = request;
  state->admission_shard = native_io_sharded_context_shard(context);
  state->admission_finalizes_seen = atomic_load(&state->finalizes);
  atomic_fetch_add(&state->admissions, 1);
}

static void native_io_sharded_route_attach(native_io_sharded_context *context, void *arg) {
  native_io_sharded_route_state *state = (native_io_sharded_route_state *)arg;
  state->attach_status = native_io_sharded_context_attach_pipe(
      context, state->native_handle, NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &state->endpoint);
}

static void native_io_sharded_route_observe(native_io_sharded_context *context, void *arg) {
  native_io_sharded_route_state *state = (native_io_sharded_route_state *)arg;
  state->observe_count = 0u;
  state->observe_status =
      native_io_sharded_context_observe(context, &state->completion, 1u, 5000u,
                                        &state->observe_count);
}

static void native_io_sharded_route_release(native_io_sharded_context *context, void *arg) {
  native_io_sharded_route_state *state = (native_io_sharded_route_state *)arg;
  state->release_status = native_io_sharded_context_release_pipe(context, state->endpoint);
}

spec("NativeIO bounded sharded routing") {
  it("routes explicit owned operations to endpoint owner without implicit transfer on rejection") {
    native_io_sharded *runtime = NULL;
    native_io_sharded *other_runtime = NULL;
    native_io_sharded_test_pipe pipe_endpoint = {0};
    native_io_sharded_route_state state = {0};
    native_io_sharded_gate_state gate = {0};
    native_io_sharded_task attach_task = {
        native_io_sharded_route_attach, NULL, NULL, &state};
    native_io_sharded_task observe_task = {
        native_io_sharded_route_observe, NULL, NULL, &state};
    native_io_sharded_task release_task = {
        native_io_sharded_route_release, NULL, NULL, &state};
    native_io_sharded_task gate_task = {
        native_io_sharded_gate_run, native_io_sharded_gate_cancel,
        native_io_sharded_gate_finalize, &gate};
    native_io_sharded_ownership ownership = {
        native_io_sharded_route_terminal, native_io_sharded_route_finalize, &state};
    int status = native_io_sharded_test_create(2u, 2u, &runtime);

    if (status == SALTS_ENOTSUP) {
      check_equal(status, SALTS_ENOTSUP);
    } else {
      native_io_sharded_operation operation;
      check_equal(status, SALTS_OK);
      check_equal(native_io_sharded_test_pipe_create(&pipe_endpoint), SALTS_OK);
      state.native_handle = pipe_endpoint.handle;
      state.peer = pipe_endpoint.peer;

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &attach_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.attach_status, SALTS_OK);
      operation = native_io_sharded_route_read_operation(&state);

      check_equal(native_io_sharded_try_submit_owned(
                      runtime, &operation, &ownership,
                      native_io_sharded_route_admission, &state),
                  SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(atomic_load(&state.admissions), 1);
      check_equal(state.admission_status, SALTS_OK);
      check_equal(state.admission_shard, (size_t)1);
      check_equal(state.admission_finalizes_seen, 0);
      check_true(native_io_sharded_request_valid(state.request));
      check_equal(native_io_sharded_request_owner_shard(state.request), (size_t)1);
      check_equal(atomic_load(&state.terminals), 0);
      check_equal(atomic_load(&state.finalizes), 0);

      {
        const unsigned char payload = 0x71u;
        state.peer_write_status =
            native_io_sharded_test_pipe_write(state.peer, &payload, sizeof(payload));
      }
      check_equal(state.peer_write_status, SALTS_OK);
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &observe_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.observe_status, SALTS_OK);
      check_equal(state.observe_count, (size_t)1);
      check_equal(state.completion.kind, NATIVE_IO_COMPLETION_OK);
      check_equal(state.completion.user_data, (uintptr_t)0x4743u);
      check_equal(atomic_load(&state.terminals), 1);
      check_equal(atomic_load(&state.finalizes), 1);
      check_equal(state.terminal_kind, NATIVE_IO_COMPLETION_OK);
      check_equal(state.terminal_bytes, (size_t)1);
      check_equal(state.terminal_byte, (unsigned char)0x71u);
      check_equal(state.terminal_shard, (size_t)1);

      check_equal(native_io_sharded_test_create(2u, 2u, &other_runtime), SALTS_OK);
      check_equal(native_io_sharded_try_submit_owned(
                      other_runtime, &operation, &ownership,
                      native_io_sharded_route_admission, &state),
                  SALTS_ENOENT);
      check_equal(atomic_load(&state.admissions), 1);
      check_equal(atomic_load(&state.finalizes), 1);
      check_equal(native_io_sharded_destroy(other_runtime), SALTS_OK);
      other_runtime = NULL;

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &gate_task), SALTS_OK);
      check_true(native_io_sharded_wait_atomic(&gate.started, 1));
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &gate_task), SALTS_OK);
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &gate_task), SALTS_OK);
      check_equal(native_io_sharded_try_submit_owned(
                      runtime, &operation, &ownership,
                      native_io_sharded_route_admission, &state),
                  SALTS_ENOBUFS);
      check_equal(atomic_load(&state.admissions), 1);
      check_equal(atomic_load(&state.finalizes), 1);
      atomic_store_explicit(&gate.gate, 1, memory_order_release);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(atomic_load(&gate.runs), 3);
      check_equal(atomic_load(&gate.finalizes), 3);

      native_io_sharded_test_pipe_close_handle(&pipe_endpoint.handle);
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &release_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.release_status, SALTS_OK);

      state.byte = 0u;
      check_equal(native_io_sharded_try_submit_owned(
                      runtime, &operation, &ownership,
                      native_io_sharded_route_admission, &state),
                  SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(atomic_load(&state.admissions), 2);
      check_equal(state.admission_status, SALTS_ENOENT);
      check_equal(state.admission_shard, (size_t)1);
      check_false(native_io_sharded_request_valid(state.request));
      check_equal(state.admission_finalizes_seen, 1);
      check_equal(atomic_load(&state.terminals), 1);
      check_equal(atomic_load(&state.finalizes), 2);

      native_io_sharded_test_pipe_close(&pipe_endpoint);
      check_equal(native_io_sharded_destroy(runtime), SALTS_OK);
    }
  }

  it("retains explicit request ownership through terminal observe and cancellation") {
    native_io_sharded *runtime = NULL;
    native_io_sharded_test_pipe pipe_endpoint = {0};
    native_io_sharded_owned_state state = {0};
    native_io_sharded_task attach_task = {
        native_io_sharded_owned_attach, NULL, NULL, &state};
    native_io_sharded_task wrong_task = {
        native_io_sharded_owned_wrong_submit, NULL, NULL, &state};
    native_io_sharded_task submit_task = {
        native_io_sharded_owned_submit, NULL, NULL, &state};
    native_io_sharded_task prepare_task = {
        native_io_sharded_owned_prepare, NULL, NULL, &state};
    native_io_sharded_task cancel_task = {
        native_io_sharded_owned_cancel, NULL, NULL, &state};
    native_io_sharded_task observe_task = {
        native_io_sharded_owned_observe, NULL, NULL, &state};
    native_io_sharded_task release_task = {
        native_io_sharded_owned_release, NULL, NULL, &state};
    int status = native_io_sharded_test_create(2u, 2u, &runtime);

    if (status == SALTS_ENOTSUP) {
      check_equal(status, SALTS_ENOTSUP);
    } else {
      check_equal(status, SALTS_OK);
      check_equal(native_io_sharded_test_pipe_create(&pipe_endpoint), SALTS_OK);
      state.native_handle = pipe_endpoint.handle;
      state.peer = pipe_endpoint.peer;

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &attach_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.attach_status, SALTS_OK);
      check_true(native_io_sharded_endpoint_valid(state.endpoint));

      check_equal(native_io_sharded_try_submit_to(runtime, 0u, &wrong_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.wrong_submit_status, SALTS_EPERM);
      check_equal(atomic_load(&state.terminals), 0);
      check_equal(atomic_load(&state.finalizes), 0);

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &submit_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.invalid_owner_status, SALTS_EINVAL);
      check_equal(state.submit_status, SALTS_OK);
      check_true(native_io_sharded_request_valid(state.request));
      check_equal(atomic_load(&state.terminals), 0);
      check_equal(atomic_load(&state.finalizes), 0);

      {
        const unsigned char payload = 0x63u;
        state.peer_write_status =
            native_io_sharded_test_pipe_write(state.peer, &payload, sizeof(payload));
      }
      check_equal(state.peer_write_status, SALTS_OK);
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &observe_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.observe_status, SALTS_OK);
      check_equal(state.observe_count, (size_t)1);
      check_equal(state.completion.kind, NATIVE_IO_COMPLETION_OK);
      check_equal(state.completion.user_data, (uintptr_t)0x4753u);
      check_equal(atomic_load(&state.terminals), 1);
      check_equal(atomic_load(&state.finalizes), 1);
      check_equal(state.terminal_finalizes_seen, 0);
      check_equal(state.nested_observe_status, SALTS_EBUSY);
      check_equal(state.terminal_kind, NATIVE_IO_COMPLETION_OK);
      check_equal(state.terminal_bytes, (size_t)1);
      check_equal(state.terminal_byte, (unsigned char)0x63u);
      check_equal(state.terminal_shard, (size_t)1);

      state.byte = 0u;
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &submit_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.submit_status, SALTS_OK);
      check_equal(atomic_load(&state.finalizes), 1);

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &cancel_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.cancel_status, SALTS_OK);
      check_equal(atomic_load(&state.finalizes), 1);

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &observe_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.observe_status, SALTS_OK);
      check_equal(state.observe_count, (size_t)1);
      check_equal(state.completion.kind, NATIVE_IO_COMPLETION_CANCELLED);
      check_equal(atomic_load(&state.terminals), 2);
      check_equal(atomic_load(&state.finalizes), 2);
      check_equal(state.terminal_finalizes_seen, 1);
      check_equal(state.nested_observe_status, SALTS_EBUSY);
      check_equal(state.terminal_kind, NATIVE_IO_COMPLETION_CANCELLED);
      check_equal(state.terminal_shard, (size_t)1);

      state.byte = 0u;
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &prepare_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.prepare_status, SALTS_OK);
      check_equal(state.flush_status, SALTS_OK);
      check_true(native_io_sharded_request_valid(state.request));
      check_equal(atomic_load(&state.finalizes), 2);

      {
        const unsigned char payload = 0x7du;
        state.peer_write_status =
            native_io_sharded_test_pipe_write(state.peer, &payload, sizeof(payload));
      }
      check_equal(state.peer_write_status, SALTS_OK);
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &observe_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.observe_status, SALTS_OK);
      check_equal(state.observe_count, (size_t)1);
      check_equal(state.completion.kind, NATIVE_IO_COMPLETION_OK);
      check_equal(atomic_load(&state.terminals), 3);
      check_equal(atomic_load(&state.finalizes), 3);
      check_equal(state.terminal_finalizes_seen, 2);
      check_equal(state.nested_observe_status, SALTS_EBUSY);
      check_equal(state.terminal_kind, NATIVE_IO_COMPLETION_OK);
      check_equal(state.terminal_bytes, (size_t)1);
      check_equal(state.terminal_byte, (unsigned char)0x7du);
      check_equal(state.terminal_shard, (size_t)1);

      native_io_sharded_test_pipe_close_handle(&pipe_endpoint.handle);
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &release_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.release_status, SALTS_OK);
      native_io_sharded_test_pipe_close(&pipe_endpoint);
      check_equal(native_io_sharded_destroy(runtime), SALTS_OK);
    }
  }

  it("binds endpoint affinity outside the raw NativeIO handle and rejects wrong-shard use") {
    native_io_sharded *runtime = NULL;
    native_io_sharded_test_pipe pipe_endpoint = {0};
    native_io_sharded_affinity_state state = {0};
    native_io_sharded_task attach_task = {
        native_io_sharded_affinity_attach, NULL, NULL, &state};
    native_io_sharded_task submit_task = {
        native_io_sharded_affinity_submit, NULL, NULL, &state};
    native_io_sharded_task wrong_task = {
        native_io_sharded_affinity_wrong_owner, NULL, NULL, &state};
    native_io_sharded_task observe_task = {
        native_io_sharded_affinity_observe, NULL, NULL, &state};
    native_io_sharded_task release_task = {
        native_io_sharded_affinity_release, NULL, NULL, &state};
    int status = native_io_sharded_test_create(2u, 2u, &runtime);

    if (status == SALTS_ENOTSUP) {
      check_equal(status, SALTS_ENOTSUP);
    } else {
      check_equal(status, SALTS_OK);
      check_equal(native_io_sharded_test_pipe_create(&pipe_endpoint), SALTS_OK);
      state.native_handle = pipe_endpoint.handle;
      state.peer = pipe_endpoint.peer;

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &attach_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.attach_status, SALTS_OK);
      check_true(native_io_sharded_endpoint_valid(state.endpoint));
      check_equal(state.observed_owner_shard, (size_t)1);

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &submit_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.owner_submit_status, SALTS_OK);
      check_true(native_io_sharded_request_valid(state.request));
      check_equal(state.observed_request_shard, (size_t)1);

      check_equal(native_io_sharded_try_submit_to(runtime, 0u, &wrong_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.wrong_submit_status, SALTS_EPERM);
      check_equal(state.wrong_cancel_status, SALTS_EPERM);
      check_equal(state.wrong_release_status, SALTS_EPERM);

      {
        const unsigned char payload = 0x5au;
        state.peer_write_status =
            native_io_sharded_test_pipe_write(state.peer, &payload, sizeof(payload));
      }
      check_equal(state.peer_write_status, SALTS_OK);
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &observe_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.owner_observe_status, SALTS_OK);
      check_equal(state.owner_observe_count, (size_t)1);
      check_equal(state.completion.kind, NATIVE_IO_COMPLETION_OK);
      check_equal(state.completion.bytes, (size_t)1);
      check_equal(state.completion.user_data, (uintptr_t)0x475u);
      check_equal(state.completion.request.owner_identity, state.request.owner_identity);
      check_equal(state.completion.request.owner_shard, state.request.owner_shard);
      check_equal(state.completion.request.native_request.slot, state.request.native_request.slot);
      check_equal(state.completion.request.native_request.generation,
                  state.request.native_request.generation);
      check_equal(state.completion.endpoint.owner_identity, state.endpoint.owner_identity);
      check_equal(state.completion.endpoint.owner_shard, state.endpoint.owner_shard);
      check_equal(state.byte, (unsigned char)0x5au);

      native_io_sharded_test_pipe_close_handle(&pipe_endpoint.handle);
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &release_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.owner_release_status, SALTS_OK);
      check_equal(state.stale_submit_status, SALTS_ENOENT);

      native_io_sharded_test_pipe_close(&pipe_endpoint);
      check_equal(native_io_sharded_destroy(runtime), SALTS_OK);
    }
  }

  it("keeps fixed affinity and executes same-shard nested dispatch directly") {
    native_io_sharded *runtime = NULL;
    native_io_sharded_direct_state state = {0};
    native_io_sharded_task outer;
    native_io_sharded_stats stats = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
    int status = native_io_sharded_test_create(2u, 2u, &runtime);

    if (status == SALTS_ENOTSUP) {
      check_equal(status, SALTS_ENOTSUP);
    } else {
      check_equal(status, SALTS_OK);
      check_not_null(runtime);
      check_equal(native_io_sharded_current_shard(runtime), SIZE_MAX);

      state.runtime = runtime;
      state.expected_shard = 1u;
      outer = (native_io_sharded_task){
          native_io_sharded_outer_run, NULL, native_io_sharded_outer_finalize, &state};
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &outer), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);

      check_equal(state.nested_status, SALTS_OK);
      check_equal(state.outer_sequence, 1);
      check_equal(state.nested_sequence, 2);
      check_equal(state.after_nested_sequence, 3);
      check_equal(atomic_load(&state.migrations), 0);
      check_equal(atomic_load(&state.nested_finalizes), 1);
      check_equal(atomic_load(&state.outer_finalizes), 1);

      check_true(native_io_sharded_get_stats(runtime, &stats));
      check_equal(stats.shard_count, (size_t)2);
      check_equal(stats.queue_capacity_per_shard, (size_t)2);
      check_equal(stats.command_slot_capacity_per_shard, (size_t)3);
      check_equal(stats.submitted_tasks, (uint64_t)2);
      check_equal(stats.same_shard_direct_tasks, (uint64_t)1);
      check_equal(stats.queued_dispatches, (uint64_t)1);
      check_equal(stats.completed_tasks, (uint64_t)2);
      check_equal(stats.cancelled_tasks, (uint64_t)0);
      check_equal(stats.active_command_slots, (uint64_t)0);
      check_equal(native_io_sharded_destroy(runtime), SALTS_OK);
    }
  }

  it("rejects beyond active-plus-queue capacity without transferring ownership") {
    native_io_sharded *runtime = NULL;
    native_io_sharded_gate_state accepted = {0};
    native_io_sharded_gate_state rejected = {0};
    native_io_sharded_task accepted_task = {
        native_io_sharded_gate_run, native_io_sharded_gate_cancel,
        native_io_sharded_gate_finalize, &accepted};
    native_io_sharded_task rejected_task = {
        native_io_sharded_gate_run, native_io_sharded_gate_cancel,
        native_io_sharded_gate_finalize, &rejected};
    native_io_sharded_stats stats = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
    int status = native_io_sharded_test_create(1u, 2u, &runtime);

    if (status == SALTS_ENOTSUP) {
      check_equal(status, SALTS_ENOTSUP);
    } else {
      check_equal(status, SALTS_OK);
      check_equal(native_io_sharded_try_submit_to(runtime, 0u, &accepted_task), SALTS_OK);
      check_true(native_io_sharded_wait_atomic(&accepted.started, 1));
      check_equal(native_io_sharded_try_submit_to(runtime, 0u, &accepted_task), SALTS_OK);
      check_equal(native_io_sharded_try_submit_to(runtime, 0u, &accepted_task), SALTS_OK);
      check_equal(native_io_sharded_try_submit_to(runtime, 0u, &rejected_task), SALTS_ENOBUFS);

      check_equal(atomic_load(&rejected.started), 0);
      check_equal(atomic_load(&rejected.runs), 0);
      check_equal(atomic_load(&rejected.cancels), 0);
      check_equal(atomic_load(&rejected.finalizes), 0);

      atomic_store_explicit(&accepted.gate, 1, memory_order_release);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(atomic_load(&accepted.runs), 3);
      check_equal(atomic_load(&accepted.cancels), 0);
      check_equal(atomic_load(&accepted.finalizes), 3);

      check_true(native_io_sharded_get_stats(runtime, &stats));
      check_equal(stats.submitted_tasks, (uint64_t)3);
      check_equal(stats.queued_dispatches, (uint64_t)3);
      check_equal(stats.rejected_tasks, (uint64_t)1);
      check_equal(stats.peak_command_slots, (uint64_t)3);
      check_equal(stats.active_command_slots, (uint64_t)0);
      check_equal(native_io_sharded_destroy(runtime), SALTS_OK);
    }
  }


  it("orders shutdown probe behind an already accepted queued owned route") {
    native_io_sharded *runtime = NULL;
    native_io_sharded_test_pipe pipe_endpoint = {0};
    native_io_sharded_route_state state = {0};
    native_io_sharded_gate_state gate = {0};
    native_io_sharded_shutdown_state shutdown = {0};
    native_io_sharded_task attach_task = {
        native_io_sharded_route_attach, NULL, NULL, &state};
    native_io_sharded_task gate_task = {
        native_io_sharded_gate_run, native_io_sharded_gate_cancel,
        native_io_sharded_gate_finalize, &gate};
    native_io_sharded_task release_task = {
        native_io_sharded_route_release, NULL, NULL, &state};
    native_io_sharded_ownership ownership = {
        native_io_sharded_route_terminal, native_io_sharded_route_finalize, &state};
    salts_thread_t shutdown_thread = {0};
    int status = native_io_sharded_test_create(1u, 2u, &runtime);

    if (status == SALTS_ENOTSUP) {
      check_equal(status, SALTS_ENOTSUP);
    } else {
      native_io_sharded_operation operation;
      check_equal(status, SALTS_OK);
      check_equal(native_io_sharded_test_pipe_create(&pipe_endpoint), SALTS_OK);
      state.native_handle = pipe_endpoint.handle;
      state.peer = pipe_endpoint.peer;

      check_equal(native_io_sharded_try_submit_to(runtime, 0u, &attach_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.attach_status, SALTS_OK);
      operation = native_io_sharded_route_read_operation(&state);

      check_equal(native_io_sharded_try_submit_to(runtime, 0u, &gate_task), SALTS_OK);
      check_true(native_io_sharded_wait_atomic(&gate.started, 1));
      check_equal(native_io_sharded_try_submit_owned(
                      runtime, &operation, &ownership,
                      native_io_sharded_route_admission, &state),
                  SALTS_OK);
      check_equal(atomic_load(&state.admissions), 0);
      check_equal(atomic_load(&state.finalizes), 0);

      shutdown.runtime = runtime;
      check_equal(
          salts_thread_create(&shutdown_thread, native_io_sharded_shutdown_thread, &shutdown),
          SALTS_OK);
      check_true(native_io_sharded_wait_atomic(&shutdown.entered, 1));
      salts_sleep_ms(10u);
      check_equal(atomic_load_explicit(&shutdown.returned, memory_order_acquire), 0);

      atomic_store_explicit(&gate.gate, 1, memory_order_release);
      check_equal(salts_thread_join(&shutdown_thread), SALTS_OK);
      check_equal(shutdown.status, SALTS_EBUSY);
      check_equal(atomic_load(&gate.runs), 1);
      check_equal(atomic_load(&gate.finalizes), 1);
      check_equal(atomic_load(&state.admissions), 1);
      check_equal(state.admission_status, SALTS_OK);
      check_equal(state.admission_shard, (size_t)0);
      check_equal(atomic_load(&state.terminals), 1);
      check_equal(atomic_load(&state.finalizes), 1);
      check_equal(state.terminal_kind, NATIVE_IO_COMPLETION_CANCELLED);
      check_equal(state.terminal_shard, (size_t)0);

      native_io_sharded_test_pipe_close_handle(&pipe_endpoint.handle);
      check_equal(native_io_sharded_try_submit_to(runtime, 0u, &release_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.release_status, SALTS_OK);
      check_equal(native_io_sharded_shutdown(runtime), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);

      native_io_sharded_test_pipe_close(&pipe_endpoint);
      check_equal(native_io_sharded_destroy(runtime), SALTS_OK);
    }
  }

  it("drains routed owned requests to terminal cancellation before recoverable endpoint busy") {
    native_io_sharded *runtime = NULL;
    native_io_sharded_test_pipe pipe_endpoint = {0};
    native_io_sharded_route_state state = {0};
    native_io_sharded_task attach_task = {
        native_io_sharded_route_attach, NULL, NULL, &state};
    native_io_sharded_task release_task = {
        native_io_sharded_route_release, NULL, NULL, &state};
    native_io_sharded_ownership ownership = {
        native_io_sharded_route_terminal, native_io_sharded_route_finalize, &state};
    native_io_sharded_stats stats = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
    int status = native_io_sharded_test_create(2u, 2u, &runtime);

    if (status == SALTS_ENOTSUP) {
      check_equal(status, SALTS_ENOTSUP);
    } else {
      native_io_sharded_operation operation;
      check_equal(status, SALTS_OK);
      check_equal(native_io_sharded_test_pipe_create(&pipe_endpoint), SALTS_OK);
      state.native_handle = pipe_endpoint.handle;
      state.peer = pipe_endpoint.peer;

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &attach_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.attach_status, SALTS_OK);
      operation = native_io_sharded_route_read_operation(&state);

      check_equal(native_io_sharded_try_submit_owned(
                      runtime, &operation, &ownership,
                      native_io_sharded_route_admission, &state),
                  SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.admission_status, SALTS_OK);
      check_true(native_io_sharded_request_valid(state.request));
      state.attempt_terminal_resubmit = 1;
      check_equal(atomic_load(&state.terminals), 0);
      check_equal(atomic_load(&state.finalizes), 0);

      check_equal(native_io_sharded_shutdown(runtime), SALTS_EBUSY);
      check_equal(atomic_load(&state.terminals), 1);
      check_equal(atomic_load(&state.finalizes), 1);
      check_equal(state.terminal_kind, NATIVE_IO_COMPLETION_CANCELLED);
      check_equal(state.terminal_shard, (size_t)1);
      check_equal(state.terminal_resubmit_status, SALTS_ESHUTDOWN);
      check_true(native_io_sharded_get_stats(runtime, &stats));
      check_true(stats.accepting);

      native_io_sharded_test_pipe_close_handle(&pipe_endpoint.handle);
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &release_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.release_status, SALTS_OK);

      check_equal(native_io_sharded_shutdown(runtime), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(native_io_sharded_shutdown(runtime), SALTS_OK);
      native_io_sharded_test_pipe_close(&pipe_endpoint);
      check_equal(native_io_sharded_destroy(runtime), SALTS_OK);
    }
  }

  it("drains prepared owned requests without requiring a public flush") {
    native_io_sharded *runtime = NULL;
    native_io_sharded_test_pipe pipe_endpoint = {0};
    native_io_sharded_owned_state state = {0};
    native_io_sharded_task attach_task = {
        native_io_sharded_owned_attach, NULL, NULL, &state};
    native_io_sharded_task prepare_task = {
        native_io_sharded_owned_prepare_only, NULL, NULL, &state};
    native_io_sharded_task release_task = {
        native_io_sharded_owned_release, NULL, NULL, &state};
#if defined(__linux__)
    const native_io_backend_kind kind = NATIVE_IO_BACKEND_IO_URING;
#else
    const native_io_backend_kind kind = native_io_sharded_test_backend();
#endif
    int status = native_io_sharded_test_create_kind(kind, 2u, 2u, &runtime);

    if (status == SALTS_ENOTSUP) {
      check_equal(status, SALTS_ENOTSUP);
    } else {
      check_equal(status, SALTS_OK);
      check_equal(native_io_sharded_test_pipe_create(&pipe_endpoint), SALTS_OK);
      state.native_handle = pipe_endpoint.handle;
      state.peer = pipe_endpoint.peer;

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &attach_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.attach_status, SALTS_OK);

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &prepare_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.prepare_status, SALTS_OK);
      check_true(native_io_sharded_request_valid(state.request));
      check_equal(atomic_load(&state.terminals), 0);
      check_equal(atomic_load(&state.finalizes), 0);

      check_equal(native_io_sharded_shutdown(runtime), SALTS_EBUSY);
      check_equal(atomic_load(&state.terminals), 1);
      check_equal(atomic_load(&state.finalizes), 1);
      check_equal(state.terminal_kind, NATIVE_IO_COMPLETION_CANCELLED);
      check_equal(state.terminal_shard, (size_t)1);

      native_io_sharded_test_pipe_close_handle(&pipe_endpoint.handle);
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &release_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.release_status, SALTS_OK);
      check_equal(native_io_sharded_shutdown(runtime), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);

      native_io_sharded_test_pipe_close(&pipe_endpoint);
      check_equal(native_io_sharded_destroy(runtime), SALTS_OK);
    }
  }

  it("does not cancel owned work when an unowned request makes shutdown recoverable") {
    native_io_sharded *runtime = NULL;
    native_io_sharded_test_pipe pipe_endpoint = {0};
    native_io_sharded_route_state owned = {0};
    native_io_sharded_affinity_state unowned = {0};
    native_io_sharded_task attach_task = {
        native_io_sharded_route_attach, NULL, NULL, &owned};
    native_io_sharded_task unowned_submit_task = {
        native_io_sharded_affinity_submit, NULL, NULL, &unowned};
    native_io_sharded_task unowned_cancel_task = {
        native_io_sharded_affinity_cancel, NULL, NULL, &unowned};
    native_io_sharded_task unowned_observe_task = {
        native_io_sharded_affinity_observe, NULL, NULL, &unowned};
    native_io_sharded_task owned_observe_task = {
        native_io_sharded_route_observe, NULL, NULL, &owned};
    native_io_sharded_task release_task = {
        native_io_sharded_route_release, NULL, NULL, &owned};
    native_io_sharded_ownership ownership = {
        native_io_sharded_route_terminal, native_io_sharded_route_finalize, &owned};
    native_io_sharded_stats stats = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
    int status = native_io_sharded_test_create(2u, 2u, &runtime);

    if (status == SALTS_ENOTSUP) {
      check_equal(status, SALTS_ENOTSUP);
    } else {
      native_io_sharded_operation operation;
      check_equal(status, SALTS_OK);
      check_equal(native_io_sharded_test_pipe_create(&pipe_endpoint), SALTS_OK);
      owned.native_handle = pipe_endpoint.handle;
      owned.peer = pipe_endpoint.peer;

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &attach_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(owned.attach_status, SALTS_OK);
      operation = native_io_sharded_route_read_operation(&owned);

      check_equal(native_io_sharded_try_submit_owned(
                      runtime, &operation, &ownership,
                      native_io_sharded_route_admission, &owned),
                  SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(owned.admission_status, SALTS_OK);
      check_true(native_io_sharded_request_valid(owned.request));

      unowned.endpoint = owned.endpoint;
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &unowned_submit_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(unowned.owner_submit_status, SALTS_OK);
      check_true(native_io_sharded_request_valid(unowned.request));

      check_equal(native_io_sharded_shutdown(runtime), SALTS_EBUSY);
      check_true(native_io_sharded_get_stats(runtime, &stats));
      check_true(stats.accepting);
      check_equal(atomic_load(&owned.terminals), 0);
      check_equal(atomic_load(&owned.finalizes), 0);
      check_equal(unowned.owner_observe_count, (size_t)0);

      {
        const unsigned char payload = 0x6bu;
        owned.peer_write_status =
            native_io_sharded_test_pipe_write(owned.peer, &payload, sizeof(payload));
      }
      check_equal(owned.peer_write_status, SALTS_OK);
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &owned_observe_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(owned.observe_status, SALTS_OK);
      check_equal(owned.observe_count, (size_t)1);
      check_equal(atomic_load(&owned.terminals), 1);
      check_equal(atomic_load(&owned.finalizes), 1);
      check_equal(owned.terminal_kind, NATIVE_IO_COMPLETION_OK);
      check_equal(owned.terminal_byte, (unsigned char)0x6bu);

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &unowned_cancel_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(unowned.owner_cancel_status, SALTS_OK);
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &unowned_observe_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(unowned.owner_observe_status, SALTS_OK);
      check_equal(unowned.owner_observe_count, (size_t)1);
      check_equal(unowned.completion.kind, NATIVE_IO_COMPLETION_CANCELLED);

      native_io_sharded_test_pipe_close_handle(&pipe_endpoint.handle);
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &release_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(owned.release_status, SALTS_OK);
      check_equal(native_io_sharded_shutdown(runtime), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      native_io_sharded_test_pipe_close(&pipe_endpoint);
      check_equal(native_io_sharded_destroy(runtime), SALTS_OK);
    }
  }

  it("returns busy without observing caller-managed unowned requests") {
    native_io_sharded *runtime = NULL;
    native_io_sharded_test_pipe pipe_endpoint = {0};
    native_io_sharded_affinity_state state = {0};
    native_io_sharded_task attach_task = {
        native_io_sharded_affinity_attach, NULL, NULL, &state};
    native_io_sharded_task submit_task = {
        native_io_sharded_affinity_submit, NULL, NULL, &state};
    native_io_sharded_task cancel_task = {
        native_io_sharded_affinity_cancel, NULL, NULL, &state};
    native_io_sharded_task observe_task = {
        native_io_sharded_affinity_observe, NULL, NULL, &state};
    native_io_sharded_task release_task = {
        native_io_sharded_affinity_release, NULL, NULL, &state};
    native_io_sharded_stats stats = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
    int status = native_io_sharded_test_create(2u, 2u, &runtime);

    if (status == SALTS_ENOTSUP) {
      check_equal(status, SALTS_ENOTSUP);
    } else {
      check_equal(status, SALTS_OK);
      check_equal(native_io_sharded_test_pipe_create(&pipe_endpoint), SALTS_OK);
      state.native_handle = pipe_endpoint.handle;
      state.peer = pipe_endpoint.peer;

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &attach_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.attach_status, SALTS_OK);

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &submit_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.owner_submit_status, SALTS_OK);
      check_true(native_io_sharded_request_valid(state.request));

      check_equal(native_io_sharded_shutdown(runtime), SALTS_EBUSY);
      check_true(native_io_sharded_get_stats(runtime, &stats));
      check_true(stats.accepting);
      check_equal(state.owner_observe_count, (size_t)0);

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &cancel_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.owner_cancel_status, SALTS_OK);

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &observe_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.owner_observe_status, SALTS_OK);
      check_equal(state.owner_observe_count, (size_t)1);
      check_equal(state.completion.kind, NATIVE_IO_COMPLETION_CANCELLED);

      native_io_sharded_test_pipe_close_handle(&pipe_endpoint.handle);
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &release_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.owner_release_status, SALTS_OK);

      check_equal(native_io_sharded_shutdown(runtime), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      native_io_sharded_test_pipe_close(&pipe_endpoint);
      check_equal(native_io_sharded_destroy(runtime), SALTS_OK);
    }
  }

  it("wakes a blocked dispatch on shutdown before owner-local teardown") {
    native_io_sharded *runtime = NULL;
    native_io_sharded_gate_state accepted = {0};
    native_io_sharded_gate_state blocked = {0};
    native_io_sharded_task accepted_task = {
        native_io_sharded_gate_run, native_io_sharded_gate_cancel,
        native_io_sharded_gate_finalize, &accepted};
    native_io_sharded_blocking_submit_state submitter = {0};
    native_io_sharded_shutdown_state shutdown = {0};
    salts_thread_t submit_thread = {0};
    salts_thread_t shutdown_thread = {0};
    int status = native_io_sharded_test_create(1u, 1u, &runtime);

    if (status == SALTS_ENOTSUP) {
      check_equal(status, SALTS_ENOTSUP);
    } else {
      check_equal(status, SALTS_OK);
      check_equal(native_io_sharded_try_submit_to(runtime, 0u, &accepted_task), SALTS_OK);
      check_true(native_io_sharded_wait_atomic(&accepted.started, 1));
      check_equal(native_io_sharded_try_submit_to(runtime, 0u, &accepted_task), SALTS_OK);

      submitter.runtime = runtime;
      submitter.shard = 0u;
      submitter.task = (native_io_sharded_task){
          native_io_sharded_gate_run, native_io_sharded_gate_cancel,
          native_io_sharded_gate_finalize, &blocked};
      check_equal(
          salts_thread_create(&submit_thread, native_io_sharded_blocking_submit_thread, &submitter),
          SALTS_OK);
      check_true(native_io_sharded_wait_atomic(&submitter.entered, 1));
      salts_sleep_ms(10u);
      check_equal(atomic_load_explicit(&submitter.returned, memory_order_acquire), 0);

      shutdown.runtime = runtime;
      check_equal(
          salts_thread_create(&shutdown_thread, native_io_sharded_shutdown_thread, &shutdown),
          SALTS_OK);
      check_true(native_io_sharded_wait_atomic(&shutdown.entered, 1));

      check_equal(salts_thread_join(&submit_thread), SALTS_OK);
      check_equal(submitter.status, SALTS_ESHUTDOWN);
      check_equal(atomic_load(&blocked.started), 0);
      check_equal(atomic_load(&blocked.runs), 0);
      check_equal(atomic_load(&blocked.cancels), 0);
      check_equal(atomic_load(&blocked.finalizes), 0);

      atomic_store_explicit(&accepted.gate, 1, memory_order_release);
      check_equal(salts_thread_join(&shutdown_thread), SALTS_OK);
      check_equal(shutdown.status, SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(atomic_load(&accepted.runs), 2);
      check_equal(atomic_load(&accepted.cancels), 0);
      check_equal(atomic_load(&accepted.finalizes), 2);
      check_equal(native_io_sharded_destroy(runtime), SALTS_OK);
    }
  }

  it("closes admission deterministically and rejects invalid targets") {
    native_io_sharded *runtime = NULL;
    native_io_sharded_stats stats = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
    atomic_int runs = 0;
    native_io_sharded_task task = {native_io_sharded_noop_run, NULL, NULL, &runs};
    int status = native_io_sharded_test_create(1u, 1u, &runtime);

    if (status == SALTS_ENOTSUP) {
      check_equal(status, SALTS_ENOTSUP);
    } else {
      check_equal(status, SALTS_OK);
      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &task), SALTS_EINVAL);
      check_equal(native_io_sharded_try_submit_to(runtime, 0u, &task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(atomic_load(&runs), 1);

      check_equal(native_io_sharded_shutdown(runtime), SALTS_OK);
      check_equal(native_io_sharded_shutdown(runtime), SALTS_OK);
      check_equal(native_io_sharded_try_submit_to(runtime, 0u, &task), SALTS_ESHUTDOWN);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);

      check_true(native_io_sharded_get_stats(runtime, &stats));
      check_false(stats.accepting);
      check_equal(stats.rejected_tasks, (uint64_t)2);
      check_equal(native_io_sharded_destroy(runtime), SALTS_OK);
    }
  }
}
