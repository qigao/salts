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

static int native_io_sharded_test_create(size_t shards, size_t queue_capacity,
                                         native_io_sharded **out_runtime) {
  const native_io_backend_kind kind = native_io_sharded_test_backend();
  native_io_sharded_config config = {
      shards, queue_capacity, {kind, 2u, 2u, 2u}};
  if (kind == (native_io_backend_kind)0 || !native_io_backend_kind_supported(kind))
    return SALTS_ENOTSUP;
  return native_io_sharded_create(&config, out_runtime);
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
  HANDLE handle;
  const int length =
      snprintf(name, sizeof(name), "\\\\.\\pipe\\native-io-sharded-%lu-%ld",
               GetCurrentProcessId(), InterlockedIncrement(&sequence));
  if (length < 0 || (size_t)length >= sizeof(name)) return SALTS_ERANGE;
  handle = CreateNamedPipeA(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1u,
                            NATIVE_IO_SHARDED_TEST_PIPE_BUFFER_CAPACITY,
                            NATIVE_IO_SHARDED_TEST_PIPE_BUFFER_CAPACITY, 0u, NULL);
  if (handle == INVALID_HANDLE_VALUE) return -(int)GetLastError();
  pipe_endpoint->handle = (uintptr_t)handle;
  pipe_endpoint->peer = 0u;
  return SALTS_OK;
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
  uintptr_t native_handle;
  unsigned char byte;
  int attach_status;
  int wrong_submit_status;
  int wrong_release_status;
  int owner_release_status;
  int stale_submit_status;
  size_t observed_owner_shard;
} native_io_sharded_affinity_state;

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

static void native_io_sharded_affinity_wrong_owner(native_io_sharded_context *context, void *arg) {
  native_io_sharded_affinity_state *state = (native_io_sharded_affinity_state *)arg;
  native_io_sharded_operation operation = native_io_sharded_affinity_read_operation(state);
  native_io_sharded_request request = {0};
  state->wrong_submit_status =
      native_io_sharded_context_submit(context, &operation, &request);
  state->wrong_release_status =
      native_io_sharded_context_release_pipe(context, state->endpoint);
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

spec("NativeIO bounded sharded routing") {
  it("binds endpoint affinity outside the raw NativeIO handle and rejects wrong-shard use") {
    native_io_sharded *runtime = NULL;
    native_io_sharded_test_pipe pipe_endpoint = {0};
    native_io_sharded_affinity_state state = {0};
    native_io_sharded_task attach_task = {
        native_io_sharded_affinity_attach, NULL, NULL, &state};
    native_io_sharded_task wrong_task = {
        native_io_sharded_affinity_wrong_owner, NULL, NULL, &state};
    native_io_sharded_task release_task = {
        native_io_sharded_affinity_release, NULL, NULL, &state};
    int status = native_io_sharded_test_create(2u, 2u, &runtime);

    if (status == SALTS_ENOTSUP) {
      check_equal(status, SALTS_ENOTSUP);
    } else {
      check_equal(status, SALTS_OK);
      check_equal(native_io_sharded_test_pipe_create(&pipe_endpoint), SALTS_OK);
      state.native_handle = pipe_endpoint.handle;

      check_equal(native_io_sharded_try_submit_to(runtime, 1u, &attach_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.attach_status, SALTS_OK);
      check_true(native_io_sharded_endpoint_valid(state.endpoint));
      check_equal(state.observed_owner_shard, (size_t)1);

      check_equal(native_io_sharded_try_submit_to(runtime, 0u, &wrong_task), SALTS_OK);
      check_equal(native_io_sharded_wait(runtime), SALTS_OK);
      check_equal(state.wrong_submit_status, SALTS_EPERM);
      check_equal(state.wrong_release_status, SALTS_EPERM);

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
