#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/native_io_sharded.h>

#include <inttypes.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  SHARDED_BENCH_DEFAULT_ITERATIONS = 50000,
  SHARDED_BENCH_DEFAULT_WARMUP = 5000,
  SHARDED_BENCH_REPLICATES = 7,
  SHARDED_BENCH_QUEUE_CAPACITY = 1024
};

typedef struct sharded_bench_task_state {
  atomic_uint_fast64_t runs;
  atomic_uint_fast64_t finalizes;
} sharded_bench_task_state;

typedef struct sharded_bench_same_driver {
  native_io_sharded *runtime;
  native_io_sharded_task inner;
  size_t iterations;
  int status;
  uint64_t wall_ns;
} sharded_bench_same_driver;

typedef struct sharded_bench_sample {
  uint64_t wall_ns;
  double ns_per_op;
  double ops_per_second;
  uint64_t same_shard_direct;
  uint64_t queued_dispatches;
  uint64_t rejected_tasks;
  uint64_t peak_command_slots;
} sharded_bench_sample;

typedef struct sharded_bench_summary {
  const char *style;
  size_t iterations;
  size_t replicates;
  double p50_ns_per_op;
  double p95_ns_per_op;
  double median_ops_per_second;
  uint64_t total_same_shard_direct;
  uint64_t total_queued_dispatches;
  uint64_t total_rejected_tasks;
  uint64_t peak_command_slots;
  unsigned message_hops_per_op;
} sharded_bench_summary;

static size_t sharded_bench_env_count(const char *name, size_t fallback) {
  const char *value = getenv(name);
  char *end = NULL;
  unsigned long long parsed;
  if (value == NULL || *value == '\0') return fallback;
  parsed = strtoull(value, &end, 10);
  if (end == value || *end != '\0' || parsed == 0u || parsed > 10000000ull)
    return fallback;
  return (size_t)parsed;
}

static native_io_backend_kind sharded_bench_backend(void) {
  const char *value = getenv("NATIVE_IO_SHARDED_BENCH_BACKEND");
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

static const char *sharded_bench_backend_name(native_io_backend_kind kind) {
  switch (kind) {
    case NATIVE_IO_BACKEND_EPOLL: return "epoll";
    case NATIVE_IO_BACKEND_IO_URING: return "io_uring";
    case NATIVE_IO_BACKEND_IOCP: return "iocp";
    case NATIVE_IO_BACKEND_KQUEUE: return "kqueue";
    default: return "unsupported";
  }
}

static void sharded_bench_task_run(native_io_sharded_context *context, void *arg) {
  sharded_bench_task_state *state = (sharded_bench_task_state *)arg;
  (void)context;
  atomic_fetch_add_explicit(&state->runs, 1u, memory_order_relaxed);
}

static void sharded_bench_task_finalize(void *arg) {
  sharded_bench_task_state *state = (sharded_bench_task_state *)arg;
  atomic_fetch_add_explicit(&state->finalizes, 1u, memory_order_relaxed);
}

static void sharded_bench_same_driver_run(native_io_sharded_context *context, void *arg) {
  sharded_bench_same_driver *driver = (sharded_bench_same_driver *)arg;
  uint64_t started;
  (void)context;

  driver->status = SALTS_OK;
  started = salts_hrtime();
  for (size_t index = 0u; index < driver->iterations; ++index) {
    const int status = native_io_sharded_try_submit_to(driver->runtime, 0u, &driver->inner);
    if (status != SALTS_OK) {
      driver->status = status;
      break;
    }
  }
  driver->wall_ns = salts_hrtime() - started;
}

static int sharded_bench_runtime_create(native_io_backend_kind kind, native_io_sharded **out) {
  native_io_sharded_config config = {
      2u, SHARDED_BENCH_QUEUE_CAPACITY, {kind, 1u, 1u, 1u}};
  return native_io_sharded_create(&config, out);
}

static uint64_t sharded_bench_delta(uint64_t after, uint64_t before) {
  return after >= before ? after - before : 0u;
}

static int sharded_bench_same_sample(native_io_sharded *runtime, size_t iterations,
                                     sharded_bench_sample *out) {
  sharded_bench_task_state task_state;
  sharded_bench_same_driver driver;
  native_io_sharded_task outer;
  native_io_sharded_stats before = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
  native_io_sharded_stats after = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
  int status;

  atomic_init(&task_state.runs, 0u);
  atomic_init(&task_state.finalizes, 0u);
  driver = (sharded_bench_same_driver){
      runtime,
      {sharded_bench_task_run, NULL, sharded_bench_task_finalize, &task_state},
      iterations,
      SALTS_OK,
      0u};
  outer = (native_io_sharded_task){sharded_bench_same_driver_run, NULL, NULL, &driver};

  if (!native_io_sharded_get_stats(runtime, &before)) return SALTS_EIO;
  status = native_io_sharded_submit_to(runtime, 0u, &outer);
  if (status != SALTS_OK) return status;
  status = native_io_sharded_wait(runtime);
  if (status != SALTS_OK) return status;
  if (driver.status != SALTS_OK) return driver.status;
  if (atomic_load_explicit(&task_state.runs, memory_order_acquire) != iterations ||
      atomic_load_explicit(&task_state.finalizes, memory_order_acquire) != iterations)
    return SALTS_EPROTO;
  if (!native_io_sharded_get_stats(runtime, &after)) return SALTS_EIO;

  out->wall_ns = driver.wall_ns;
  out->ns_per_op = iterations == 0u ? 0.0 : (double)driver.wall_ns / (double)iterations;
  out->ops_per_second =
      driver.wall_ns == 0u ? 0.0 : (double)iterations * 1.0e9 / (double)driver.wall_ns;
  out->same_shard_direct =
      sharded_bench_delta(after.same_shard_direct_tasks, before.same_shard_direct_tasks);
  /* One queued outer harness task is excluded from the measured same-owner path. */
  out->queued_dispatches =
      sharded_bench_delta(after.queued_dispatches, before.queued_dispatches);
  if (out->queued_dispatches != 1u || out->same_shard_direct != iterations)
    return SALTS_EPROTO;
  out->queued_dispatches = 0u;
  out->rejected_tasks = sharded_bench_delta(after.rejected_tasks, before.rejected_tasks);
  out->peak_command_slots = after.peak_command_slots;
  return out->rejected_tasks == 0u ? SALTS_OK : SALTS_EPROTO;
}

static int sharded_bench_cross_sample(native_io_sharded *runtime, size_t iterations,
                                      sharded_bench_sample *out) {
  sharded_bench_task_state state;
  native_io_sharded_task task;
  native_io_sharded_stats before = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
  native_io_sharded_stats after = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
  uint64_t started;
  int status = SALTS_OK;

  atomic_init(&state.runs, 0u);
  atomic_init(&state.finalizes, 0u);
  task = (native_io_sharded_task){
      sharded_bench_task_run, NULL, sharded_bench_task_finalize, &state};

  if (!native_io_sharded_get_stats(runtime, &before)) return SALTS_EIO;
  started = salts_hrtime();
  for (size_t index = 0u; index < iterations; ++index) {
    status = native_io_sharded_submit_to(runtime, 1u, &task);
    if (status != SALTS_OK) break;
  }
  if (status == SALTS_OK) status = native_io_sharded_wait(runtime);
  out->wall_ns = salts_hrtime() - started;
  if (status != SALTS_OK) return status;
  if (atomic_load_explicit(&state.runs, memory_order_acquire) != iterations ||
      atomic_load_explicit(&state.finalizes, memory_order_acquire) != iterations)
    return SALTS_EPROTO;
  if (!native_io_sharded_get_stats(runtime, &after)) return SALTS_EIO;

  out->ns_per_op = iterations == 0u ? 0.0 : (double)out->wall_ns / (double)iterations;
  out->ops_per_second =
      out->wall_ns == 0u ? 0.0 : (double)iterations * 1.0e9 / (double)out->wall_ns;
  out->same_shard_direct =
      sharded_bench_delta(after.same_shard_direct_tasks, before.same_shard_direct_tasks);
  out->queued_dispatches =
      sharded_bench_delta(after.queued_dispatches, before.queued_dispatches);
  out->rejected_tasks = sharded_bench_delta(after.rejected_tasks, before.rejected_tasks);
  out->peak_command_slots = after.peak_command_slots;
  if (out->same_shard_direct != 0u || out->queued_dispatches != iterations ||
      out->rejected_tasks != 0u)
    return SALTS_EPROTO;
  return SALTS_OK;
}

static int sharded_bench_compare_double(const void *left, const void *right) {
  const double a = *(const double *)left;
  const double b = *(const double *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static double sharded_bench_percentile(double *values, size_t count, unsigned percentile) {
  size_t index;
  qsort(values, count, sizeof(*values), sharded_bench_compare_double);
  index = ((count - 1u) * (size_t)percentile + 50u) / 100u;
  return values[index];
}

static int sharded_bench_run_style(native_io_backend_kind kind, const char *style,
                                   size_t warmup, size_t iterations, bool same_owner,
                                   sharded_bench_summary *out) {
  native_io_sharded *runtime = NULL;
  sharded_bench_sample warmup_sample = {0};
  sharded_bench_sample samples[SHARDED_BENCH_REPLICATES];
  double latency[SHARDED_BENCH_REPLICATES];
  double rate[SHARDED_BENCH_REPLICATES];
  uint64_t total_same = 0u;
  uint64_t total_queued = 0u;
  uint64_t total_rejected = 0u;
  uint64_t peak = 0u;
  int status = sharded_bench_runtime_create(kind, &runtime);

  if (status != SALTS_OK) return status;
  status = same_owner
               ? sharded_bench_same_sample(runtime, warmup, &warmup_sample)
               : sharded_bench_cross_sample(runtime, warmup, &warmup_sample);
  if (status != SALTS_OK) goto cleanup;

  for (size_t replicate = 0u; replicate < SHARDED_BENCH_REPLICATES; ++replicate) {
    status = same_owner
                 ? sharded_bench_same_sample(runtime, iterations, &samples[replicate])
                 : sharded_bench_cross_sample(runtime, iterations, &samples[replicate]);
    if (status != SALTS_OK) goto cleanup;
    latency[replicate] = samples[replicate].ns_per_op;
    rate[replicate] = samples[replicate].ops_per_second;
    total_same += samples[replicate].same_shard_direct;
    total_queued += samples[replicate].queued_dispatches;
    total_rejected += samples[replicate].rejected_tasks;
    if (samples[replicate].peak_command_slots > peak)
      peak = samples[replicate].peak_command_slots;
  }

  out->style = style;
  out->iterations = iterations;
  out->replicates = SHARDED_BENCH_REPLICATES;
  out->p50_ns_per_op =
      sharded_bench_percentile(latency, SHARDED_BENCH_REPLICATES, 50u);
  out->p95_ns_per_op =
      sharded_bench_percentile(latency, SHARDED_BENCH_REPLICATES, 95u);
  out->median_ops_per_second =
      sharded_bench_percentile(rate, SHARDED_BENCH_REPLICATES, 50u);
  out->total_same_shard_direct = total_same;
  out->total_queued_dispatches = total_queued;
  out->total_rejected_tasks = total_rejected;
  out->peak_command_slots = peak;
  out->message_hops_per_op = same_owner ? 0u : 1u;

cleanup:
  {
    const int destroy_status = native_io_sharded_destroy(runtime);
    if (status == SALTS_OK) status = destroy_status;
  }
  return status;
}

static FILE *sharded_bench_open_csv(void) {
  const char *prefix = getenv("NATIVE_IO_SHARDED_BENCH_OUTPUT");
  char path[1024];
  if (prefix == NULL || *prefix == '\0') return NULL;
  if (snprintf(path, sizeof(path), "%s.csv", prefix) < 0) return NULL;
  return fopen(path, "w");
}

static void sharded_bench_print_row(FILE *stream, const char *backend,
                                    const sharded_bench_summary *summary) {
  const uint64_t total_ops = (uint64_t)summary->iterations * (uint64_t)summary->replicates;
  fprintf(stream,
          "%s,%s,%zu,%zu,%.6f,%.6f,%.6f,%u,%" PRIu64 ",%" PRIu64
          ",%" PRIu64 ",%" PRIu64 "\n",
          backend, summary->style, summary->iterations, summary->replicates,
          summary->p50_ns_per_op, summary->p95_ns_per_op,
          summary->median_ops_per_second, summary->message_hops_per_op,
          summary->total_same_shard_direct, summary->total_queued_dispatches,
          summary->total_rejected_tasks, summary->peak_command_slots);
  (void)total_ops;
}

int main(void) {
  const native_io_backend_kind kind = sharded_bench_backend();
  const char *backend = sharded_bench_backend_name(kind);
  const size_t warmup =
      sharded_bench_env_count("NATIVE_IO_SHARDED_BENCH_WARMUP",
                              SHARDED_BENCH_DEFAULT_WARMUP);
  const size_t iterations =
      sharded_bench_env_count("NATIVE_IO_SHARDED_BENCH_ITERATIONS",
                              SHARDED_BENCH_DEFAULT_ITERATIONS);
  sharded_bench_summary same = {0};
  sharded_bench_summary cross = {0};
  FILE *csv;
  int status;

  if (kind == (native_io_backend_kind)0 || !native_io_backend_kind_supported(kind)) {
    fprintf(stderr, "unsupported NativeIO sharded benchmark backend: %s\n", backend);
    return 2;
  }

  status = sharded_bench_run_style(kind, "same_owner", warmup, iterations, true, &same);
  if (status != SALTS_OK) {
    fprintf(stderr, "same-owner benchmark failed: %d\n", status);
    return 1;
  }
  status = sharded_bench_run_style(kind, "cross_owner", warmup, iterations, false, &cross);
  if (status != SALTS_OK) {
    fprintf(stderr, "cross-owner benchmark failed: %d\n", status);
    return 1;
  }

  printf("# NativeIO Sharded routing benchmark\n\n");
  printf("Backend: %s\n\n", backend);
  printf("p50/p95 are distributions of replicate batch-average ns/op, not individual-operation latency percentiles.\n\n");
  printf("| style | iterations/replicate | replicates | p50 ns/op | p95 ns/op | median ops/s | message hops/op | same-owner direct tasks | queued dispatches | rejected | peak command slots |\n");
  printf("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  printf("| %s | %zu | %zu | %.3f | %.3f | %.0f | %u | %" PRIu64 " | %" PRIu64 " | %" PRIu64 " | %" PRIu64 " |\n",
         same.style, same.iterations, same.replicates, same.p50_ns_per_op,
         same.p95_ns_per_op, same.median_ops_per_second, same.message_hops_per_op,
         same.total_same_shard_direct, same.total_queued_dispatches,
         same.total_rejected_tasks, same.peak_command_slots);
  printf("| %s | %zu | %zu | %.3f | %.3f | %.0f | %u | %" PRIu64 " | %" PRIu64 " | %" PRIu64 " | %" PRIu64 " |\n",
         cross.style, cross.iterations, cross.replicates, cross.p50_ns_per_op,
         cross.p95_ns_per_op, cross.median_ops_per_second, cross.message_hops_per_op,
         cross.total_same_shard_direct, cross.total_queued_dispatches,
         cross.total_rejected_tasks, cross.peak_command_slots);

  csv = sharded_bench_open_csv();
  if (csv != NULL) {
    fprintf(csv,
            "backend,style,iterations_per_replicate,replicates,p50_batch_ns_per_op,"
            "p95_batch_ns_per_op,median_ops_per_second,message_hops_per_op,"
            "same_shard_direct_tasks,queued_dispatches,rejected_tasks,peak_command_slots\n");
    sharded_bench_print_row(csv, backend, &same);
    sharded_bench_print_row(csv, backend, &cross);
    fclose(csv);
  }

  return 0;
}
