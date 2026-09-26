#include "tinytest.h"

#include <salts/error_codes.h>
#include <salts/native_io_sharded.h>
#include <salts/thread.h>

#include <stdatomic.h>
#include <stdint.h>

enum { NATIVE_IO_SHARDED_TEST_WAIT_ROUNDS = 2000 };

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

spec("NativeIO bounded sharded routing") {
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
