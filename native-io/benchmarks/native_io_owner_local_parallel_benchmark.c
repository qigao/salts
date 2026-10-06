#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/native_io_sharded.h>
#include <salts/thread.h>

#include <inttypes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  OWNER_LOCAL_PARALLEL_DEFAULT_UNITS_PER_LANE = 1000000,
  OWNER_LOCAL_PARALLEL_REPLICATES = 11,
  OWNER_LOCAL_PARALLEL_QUEUE_CAPACITY = 8,
  OWNER_LOCAL_PARALLEL_READY_WAIT_ROUNDS = 5000
};

typedef struct owner_local_parallel_gate {
  atomic_size_t ready;
  atomic_int start;
  atomic_int failure_status;
} owner_local_parallel_gate;

typedef struct owner_local_parallel_task_state {
  owner_local_parallel_gate *gate;
  size_t expected_shard;
  size_t first_lane;
  size_t lane_count;
  size_t units_per_lane;
  atomic_uint_fast64_t checksum;
} owner_local_parallel_task_state;

typedef struct owner_local_parallel_summary {
  const char *mode;
  size_t units_per_lane;
  size_t total_units;
  size_t replicates;
  double p50_ns_per_unit;
  double p95_ns_per_unit;
  double median_units_per_second;
  uint64_t checksum;
} owner_local_parallel_summary;

static size_t owner_local_parallel_env_count(const char *name,
                                             size_t fallback) {
  const char *value = getenv(name);
  char *end = NULL;
  unsigned long long parsed;

  if (value == NULL || *value == '\0') return fallback;
  parsed = strtoull(value, &end, 10);
  if (end == value || *end != '\0' || parsed == 0u ||
      parsed > 100000000ull)
    return fallback;
  return (size_t)parsed;
}

static native_io_backend_kind owner_local_parallel_backend(void) {
  const char *value = getenv("NATIVE_IO_OWNER_LOCAL_PARALLEL_BACKEND");

  if (value == NULL || *value == '\0') {
#if defined(_WIN32)
    return NATIVE_IO_BACKEND_IOCP;
#elif defined(__linux__)
    return NATIVE_IO_BACKEND_EPOLL;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__) || defined(__DragonFly__)
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

static const char *owner_local_parallel_backend_name(
    native_io_backend_kind kind) {
  switch (kind) {
    case NATIVE_IO_BACKEND_EPOLL:
      return "epoll";
    case NATIVE_IO_BACKEND_IO_URING:
      return "io_uring";
    case NATIVE_IO_BACKEND_IOCP:
      return "iocp";
    case NATIVE_IO_BACKEND_KQUEUE:
      return "kqueue";
    default:
      return "unsupported";
  }
}

static uint64_t owner_local_parallel_kernel(size_t lane, size_t units) {
  uint64_t value =
      UINT64_C(0x9e3779b97f4a7c15) ^
      ((uint64_t)lane * UINT64_C(0xd1b54a32d192ed03));

  for (size_t index = 0u; index < units; ++index) {
    value ^= value << 13u;
    value ^= value >> 7u;
    value ^= value << 17u;
    value += UINT64_C(0x94d049bb133111eb) + (uint64_t)index;
  }
  return value;
}

static void owner_local_parallel_fail(owner_local_parallel_gate *gate,
                                      int status) {
  int expected = SALTS_OK;
  if (status == SALTS_OK) status = SALTS_EIO;
  (void)atomic_compare_exchange_strong_explicit(
      &gate->failure_status, &expected, status, memory_order_release,
      memory_order_relaxed);
}

static void owner_local_parallel_run(native_io_sharded_context *context,
                                     void *arg) {
  owner_local_parallel_task_state *state =
      (owner_local_parallel_task_state *)arg;
  uint64_t checksum = 0u;

  if (native_io_sharded_context_shard(context) != state->expected_shard)
    owner_local_parallel_fail(state->gate, SALTS_EPROTO);

  atomic_fetch_add_explicit(&state->gate->ready, 1u, memory_order_release);
  while (atomic_load_explicit(&state->gate->start, memory_order_acquire) == 0 &&
         atomic_load_explicit(&state->gate->failure_status,
                              memory_order_acquire) == SALTS_OK)
    cmeta_thread_yield();

  if (atomic_load_explicit(&state->gate->failure_status,
                           memory_order_acquire) != SALTS_OK)
    return;

  for (size_t lane = 0u; lane < state->lane_count; ++lane)
    checksum ^= owner_local_parallel_kernel(state->first_lane + lane,
                                            state->units_per_lane);

  atomic_store_explicit(&state->checksum, checksum, memory_order_release);
}

static void owner_local_parallel_cancel(void *arg, int status) {
  owner_local_parallel_task_state *state =
      (owner_local_parallel_task_state *)arg;
  owner_local_parallel_fail(state->gate, status);
}

static int owner_local_parallel_wait_ready(owner_local_parallel_gate *gate,
                                           size_t expected_ready) {
  for (int round = 0; round < OWNER_LOCAL_PARALLEL_READY_WAIT_ROUNDS;
       ++round) {
    if (atomic_load_explicit(&gate->failure_status,
                             memory_order_acquire) != SALTS_OK)
      return 0;
    if (atomic_load_explicit(&gate->ready, memory_order_acquire) ==
        expected_ready)
      return 1;
    cmeta_sleep_ms(1u);
  }
  return 0;
}

static native_io_sharded *owner_local_parallel_runtime_create(
    native_io_backend_kind kind, size_t shard_count) {
  const native_io_sharded_config config = {
      shard_count,
      OWNER_LOCAL_PARALLEL_QUEUE_CAPACITY,
      {kind, 1u, 1u, 1u}};
  native_io_sharded *runtime = NULL;

  return native_io_sharded_create(&config, &runtime) == SALTS_OK
             ? runtime
             : NULL;
}

static int owner_local_parallel_compare_double(const void *left,
                                               const void *right) {
  const double a = *(const double *)left;
  const double b = *(const double *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static double owner_local_parallel_percentile(double *values, size_t count,
                                              unsigned percentile) {
  size_t index;
  qsort(values, count, sizeof(*values), owner_local_parallel_compare_double);
  index = ((count - 1u) * (size_t)percentile + 50u) / 100u;
  return values[index];
}

static int owner_local_parallel_run_mode(
    native_io_backend_kind kind, const char *mode, size_t units_per_lane,
    uint64_t expected_checksum, owner_local_parallel_summary *out) {
  const int parallel = strcmp(mode, "two_owner_parallel") == 0;
  const size_t shard_count = parallel ? 2u : 1u;
  const size_t task_count = parallel ? 2u : 1u;
  native_io_sharded *runtime =
      owner_local_parallel_runtime_create(kind, shard_count);
  double latency[OWNER_LOCAL_PARALLEL_REPLICATES];
  double rate[OWNER_LOCAL_PARALLEL_REPLICATES];
  int status = SALTS_OK;

  if (runtime == NULL || out == NULL) {
    if (runtime != NULL) (void)native_io_sharded_destroy(runtime);
    return SALTS_ENOMEM;
  }

  for (size_t replicate = 0u;
       replicate < OWNER_LOCAL_PARALLEL_REPLICATES; ++replicate) {
    owner_local_parallel_gate gate;
    owner_local_parallel_task_state states[2];
    native_io_sharded_task tasks[2];
    uint64_t started;
    uint64_t elapsed;
    uint64_t checksum = 0u;

    atomic_init(&gate.ready, 0u);
    atomic_init(&gate.start, 0);
    atomic_init(&gate.failure_status, SALTS_OK);
    memset(states, 0, sizeof(states));
    memset(tasks, 0, sizeof(tasks));

    if (parallel) {
      for (size_t shard = 0u; shard < 2u; ++shard) {
        states[shard].gate = &gate;
        states[shard].expected_shard = shard;
        states[shard].first_lane = shard;
        states[shard].lane_count = 1u;
        states[shard].units_per_lane = units_per_lane;
        atomic_init(&states[shard].checksum, 0u);
        tasks[shard] = (native_io_sharded_task){
            owner_local_parallel_run, owner_local_parallel_cancel, NULL,
            &states[shard]};
        status = native_io_sharded_try_submit_to(runtime, shard, &tasks[shard]);
        if (status != SALTS_OK) break;
      }
    } else {
      states[0].gate = &gate;
      states[0].expected_shard = 0u;
      states[0].first_lane = 0u;
      states[0].lane_count = 2u;
      states[0].units_per_lane = units_per_lane;
      atomic_init(&states[0].checksum, 0u);
      tasks[0] = (native_io_sharded_task){
          owner_local_parallel_run, owner_local_parallel_cancel, NULL,
          &states[0]};
      status = native_io_sharded_try_submit_to(runtime, 0u, &tasks[0]);
    }

    if (status != SALTS_OK ||
        !owner_local_parallel_wait_ready(&gate, task_count)) {
      if (status == SALTS_OK)
        status = atomic_load_explicit(&gate.failure_status,
                                      memory_order_acquire);
      if (status == SALTS_OK) status = SALTS_ETIMEDOUT;
      atomic_store_explicit(&gate.start, 1, memory_order_release);
      (void)native_io_sharded_wait(runtime);
      break;
    }

    started = cmeta_hrtime();
    atomic_store_explicit(&gate.start, 1, memory_order_release);
    status = native_io_sharded_wait(runtime);
    elapsed = cmeta_hrtime() - started;
    if (status == SALTS_OK)
      status = atomic_load_explicit(&gate.failure_status,
                                    memory_order_acquire);
    if (status != SALTS_OK) break;

    for (size_t index = 0u; index < task_count; ++index)
      checksum ^= atomic_load_explicit(&states[index].checksum,
                                       memory_order_acquire);
    if (checksum != expected_checksum || elapsed == 0u) {
      status = SALTS_EPROTO;
      break;
    }

    latency[replicate] =
        (double)elapsed / (double)(2u * units_per_lane);
    rate[replicate] =
        (double)(2u * units_per_lane) * 1.0e9 / (double)elapsed;
  }

  if (status == SALTS_OK) {
    out->mode = mode;
    out->units_per_lane = units_per_lane;
    out->total_units = 2u * units_per_lane;
    out->replicates = OWNER_LOCAL_PARALLEL_REPLICATES;
    out->p50_ns_per_unit =
        owner_local_parallel_percentile(
            latency, OWNER_LOCAL_PARALLEL_REPLICATES, 50u);
    out->p95_ns_per_unit =
        owner_local_parallel_percentile(
            latency, OWNER_LOCAL_PARALLEL_REPLICATES, 95u);
    out->median_units_per_second =
        owner_local_parallel_percentile(
            rate, OWNER_LOCAL_PARALLEL_REPLICATES, 50u);
    out->checksum = expected_checksum;
  }

  {
    const int destroy_status = native_io_sharded_destroy(runtime);
    if (status == SALTS_OK) status = destroy_status;
  }
  return status;
}

static FILE *owner_local_parallel_open_csv(void) {
  const char *prefix = getenv("NATIVE_IO_OWNER_LOCAL_PARALLEL_OUTPUT");
  char path[1024];

  if (prefix == NULL || *prefix == '\0') return NULL;
  if (snprintf(path, sizeof(path), "%s.csv", prefix) < 0) return NULL;
  return fopen(path, "w");
}

static void owner_local_parallel_print_csv(
    FILE *stream, const char *backend, const char *cpu_set,
    const owner_local_parallel_summary *row) {
  fprintf(
      stream,
      "%s,%s,%s,%zu,%zu,%zu,%.6f,%.6f,%.6f,%" PRIu64 "\n",
      backend, cpu_set, row->mode, row->units_per_lane, row->total_units,
      row->replicates, row->p50_ns_per_unit, row->p95_ns_per_unit,
      row->median_units_per_second, row->checksum);
}

int main(void) {
  const native_io_backend_kind kind = owner_local_parallel_backend();
  const char *backend = owner_local_parallel_backend_name(kind);
  const char *cpu_set = getenv("NATIVE_IO_OWNER_LOCAL_PARALLEL_CPU_SET");
  const size_t units_per_lane =
      owner_local_parallel_env_count(
          "NATIVE_IO_OWNER_LOCAL_PARALLEL_UNITS_PER_LANE",
          OWNER_LOCAL_PARALLEL_DEFAULT_UNITS_PER_LANE);
  const uint64_t expected_checksum =
      owner_local_parallel_kernel(0u, units_per_lane) ^
      owner_local_parallel_kernel(1u, units_per_lane);
  owner_local_parallel_summary serial = {0};
  owner_local_parallel_summary parallel = {0};
  FILE *csv;
  int status;

  if (cpu_set == NULL || *cpu_set == '\0') cpu_set = "default";
  if (kind == (native_io_backend_kind)0 ||
      !native_io_backend_kind_supported(kind)) {
    fprintf(stderr,
            "unsupported NativeIO owner-local parallel backend: %s\n",
            backend);
    return 2;
  }

  status = owner_local_parallel_run_mode(
      kind, "one_owner_serial", units_per_lane, expected_checksum, &serial);
  if (status == SALTS_OK)
    status = owner_local_parallel_run_mode(
        kind, "two_owner_parallel", units_per_lane, expected_checksum,
        &parallel);
  if (status != SALTS_OK) {
    fprintf(stderr,
            "owner-local parallel benchmark failed: status=%d\n", status);
    return 1;
  }

  printf("# NativeIO owner-local parallel throughput benchmark\n\n");
  printf("Backend: %s\n\n", backend);
  printf("CPU set: %s\n\n", cpu_set);
  printf("Each mode executes the same two deterministic compute lanes. "
         "The measured interval starts only after every owner callback has "
         "entered its start gate; no measured owner-to-owner submission or "
         "I/O occurs.\n\n");
  printf("| mode | units/lane | total units | replicates | p50 ns/unit | "
         "p95 ns/unit | median units/s | checksum |\n");
  printf("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  printf("| %s | %zu | %zu | %zu | %.3f | %.3f | %.0f | %" PRIu64 " |\n",
         serial.mode, serial.units_per_lane, serial.total_units,
         serial.replicates, serial.p50_ns_per_unit, serial.p95_ns_per_unit,
         serial.median_units_per_second, serial.checksum);
  printf("| %s | %zu | %zu | %zu | %.3f | %.3f | %.0f | %" PRIu64 " |\n",
         parallel.mode, parallel.units_per_lane, parallel.total_units,
         parallel.replicates, parallel.p50_ns_per_unit,
         parallel.p95_ns_per_unit, parallel.median_units_per_second,
         parallel.checksum);
  printf("\nParallel speedup: %.3fx\n",
         parallel.median_units_per_second /
             serial.median_units_per_second);

  csv = owner_local_parallel_open_csv();
  if (csv != NULL) {
    fprintf(csv,
            "backend,cpu_set,mode,units_per_lane,total_units,replicates,"
            "p50_ns_per_unit,p95_ns_per_unit,median_units_per_second,"
            "checksum\n");
    owner_local_parallel_print_csv(csv, backend, cpu_set, &serial);
    owner_local_parallel_print_csv(csv, backend, cpu_set, &parallel);
    fclose(csv);
  }

  return 0;
}
