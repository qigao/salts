#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/native_io_sharded.h>

#include <inttypes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  OWNER_HANDOFF_DEFAULT_HOPS = 10000,
  OWNER_HANDOFF_DEFAULT_WARMUP_HOPS = 1000,
  OWNER_HANDOFF_REPLICATES = 11,
  OWNER_HANDOFF_QUEUE_CAPACITY = 1024
};

typedef struct owner_handoff_state owner_handoff_state;

typedef struct owner_handoff_chain {
  owner_handoff_state *state;
  native_io_sharded_task task;
  size_t remaining;
} owner_handoff_chain;

struct owner_handoff_state {
  native_io_sharded *runtime;
  owner_handoff_chain *chains;
  size_t chain_count;
  atomic_uint_fast64_t completed_hops;
  atomic_int failure_status;
};

typedef struct owner_handoff_sample {
  uint64_t wall_ns;
  double ns_per_hop;
  double hops_per_second;
  uint64_t queued_dispatches;
  uint64_t rejected_tasks;
  uint64_t same_shard_direct_tasks;
  uint64_t peak_command_slots;
} owner_handoff_sample;

typedef struct owner_handoff_summary {
  size_t window;
  size_t hops;
  size_t replicates;
  double p50_ns_per_hop;
  double p95_ns_per_hop;
  double median_hops_per_second;
  uint64_t total_queued_dispatches;
  uint64_t total_rejected_tasks;
  uint64_t total_same_shard_direct_tasks;
  uint64_t peak_command_slots;
} owner_handoff_summary;

static size_t owner_handoff_env_count(const char *name, size_t fallback) {
  const char *value = getenv(name);
  char *end = NULL;
  unsigned long long parsed;
  if (value == NULL || *value == '\0') return fallback;
  parsed = strtoull(value, &end, 10);
  if (end == value || *end != '\0' || parsed == 0u || parsed > 10000000ull)
    return fallback;
  return (size_t)parsed;
}

static native_io_backend_kind owner_handoff_backend(void) {
  const char *value = getenv("NATIVE_IO_OWNER_HANDOFF_BACKEND");
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

static const char *owner_handoff_backend_name(native_io_backend_kind kind) {
  switch (kind) {
    case NATIVE_IO_BACKEND_EPOLL: return "epoll";
    case NATIVE_IO_BACKEND_IO_URING: return "io_uring";
    case NATIVE_IO_BACKEND_IOCP: return "iocp";
    case NATIVE_IO_BACKEND_KQUEUE: return "kqueue";
    default: return "unsupported";
  }
}

static void owner_handoff_fail(owner_handoff_state *state, int status) {
  int expected = SALTS_OK;
  if (status == SALTS_OK) status = SALTS_EIO;
  (void)atomic_compare_exchange_strong_explicit(
      &state->failure_status, &expected, status, memory_order_release, memory_order_relaxed);
}

static void owner_handoff_cancel(void *arg, int status) {
  owner_handoff_chain *chain = (owner_handoff_chain *)arg;
  owner_handoff_fail(chain->state, status);
}

static void owner_handoff_run(native_io_sharded_context *context, void *arg) {
  owner_handoff_chain *chain = (owner_handoff_chain *)arg;
  owner_handoff_state *state = chain->state;
  const size_t shard = native_io_sharded_context_shard(context);
  size_t next;
  int status;

  if (chain->remaining == 0u || shard > 1u) {
    owner_handoff_fail(state, SALTS_EPROTO);
    return;
  }

  --chain->remaining;
  atomic_fetch_add_explicit(&state->completed_hops, 1u, memory_order_relaxed);
  if (chain->remaining == 0u) return;

  next = shard == 0u ? 1u : 0u;
  status = native_io_sharded_submit_to(state->runtime, next, &chain->task);
  if (status != SALTS_OK) owner_handoff_fail(state, status);
}

static void owner_handoff_seed(native_io_sharded_context *context, void *arg) {
  owner_handoff_state *state = (owner_handoff_state *)arg;
  int status = SALTS_OK;

  if (native_io_sharded_context_shard(context) != 0u) {
    owner_handoff_fail(state, SALTS_EPROTO);
    return;
  }

  for (size_t index = 0u; index < state->chain_count; ++index) {
    status = native_io_sharded_submit_to(state->runtime, 1u, &state->chains[index].task);
    if (status != SALTS_OK) {
      owner_handoff_fail(state, status);
      return;
    }
  }
}

static void owner_handoff_seed_cancel(void *arg, int status) {
  owner_handoff_fail((owner_handoff_state *)arg, status);
}

static uint64_t owner_handoff_delta(uint64_t after, uint64_t before) {
  return after >= before ? after - before : 0u;
}

static int owner_handoff_runtime_create(native_io_backend_kind kind,
                                        native_io_sharded **out_runtime) {
  native_io_sharded_config config = {
      2u, OWNER_HANDOFF_QUEUE_CAPACITY, {kind, 1u, 1u, 1u}};
  return native_io_sharded_create(&config, out_runtime);
}

static int owner_handoff_sample_run(native_io_sharded *runtime, size_t hops, size_t window,
                                    owner_handoff_sample *out) {
  owner_handoff_chain *chains = NULL;
  owner_handoff_state state;
  native_io_sharded_task seed;
  native_io_sharded_stats before = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
  native_io_sharded_stats after = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
  uint64_t started;
  uint64_t queued;
  int status = SALTS_OK;

  if (window == 0u || hops < window) return SALTS_EINVAL;
  chains = (owner_handoff_chain *)calloc(window, sizeof(*chains));
  if (chains == NULL) return SALTS_ENOMEM;

  state.runtime = runtime;
  state.chains = chains;
  state.chain_count = window;
  atomic_init(&state.completed_hops, 0u);
  atomic_init(&state.failure_status, SALTS_OK);

  {
    const size_t base = hops / window;
    const size_t extra = hops % window;
    for (size_t index = 0u; index < window; ++index) {
      chains[index].state = &state;
      chains[index].remaining = base + (index < extra ? 1u : 0u);
      chains[index].task = (native_io_sharded_task){
          owner_handoff_run, owner_handoff_cancel, NULL, &chains[index]};
    }
  }

  seed = (native_io_sharded_task){
      owner_handoff_seed, owner_handoff_seed_cancel, NULL, &state};

  if (!native_io_sharded_get_stats(runtime, &before)) {
    free(chains);
    return SALTS_EIO;
  }

  started = salts_hrtime();
  status = native_io_sharded_submit_to(runtime, 0u, &seed);
  if (status == SALTS_OK) status = native_io_sharded_wait(runtime);
  out->wall_ns = salts_hrtime() - started;

  if (status == SALTS_OK)
    status = atomic_load_explicit(&state.failure_status, memory_order_acquire);
  if (status == SALTS_OK &&
      atomic_load_explicit(&state.completed_hops, memory_order_acquire) != hops)
    status = SALTS_EPROTO;
  if (status == SALTS_OK && !native_io_sharded_get_stats(runtime, &after))
    status = SALTS_EIO;

  if (status == SALTS_OK) {
    queued = owner_handoff_delta(after.queued_dispatches, before.queued_dispatches);
    /*
     * The control thread queues exactly one seed task to shard 0. Every
     * additional queued dispatch is emitted from one owner callback to the
     * other owner and is therefore a measured owner-to-owner hop.
     */
    if (queued != (uint64_t)hops + 1u) {
      status = SALTS_EPROTO;
    } else {
      out->queued_dispatches = queued - 1u;
      out->rejected_tasks =
          owner_handoff_delta(after.rejected_tasks, before.rejected_tasks);
      out->same_shard_direct_tasks =
          owner_handoff_delta(after.same_shard_direct_tasks, before.same_shard_direct_tasks);
      out->peak_command_slots = after.peak_command_slots;
      out->ns_per_hop =
          hops == 0u ? 0.0 : (double)out->wall_ns / (double)hops;
      out->hops_per_second =
          out->wall_ns == 0u ? 0.0 : (double)hops * 1.0e9 / (double)out->wall_ns;
      if (out->rejected_tasks != 0u || out->same_shard_direct_tasks != 0u)
        status = SALTS_EPROTO;
    }
  }

  free(chains);
  return status;
}

static int owner_handoff_compare_double(const void *left, const void *right) {
  const double a = *(const double *)left;
  const double b = *(const double *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static double owner_handoff_percentile(double *values, size_t count, unsigned percentile) {
  size_t index;
  qsort(values, count, sizeof(*values), owner_handoff_compare_double);
  index = ((count - 1u) * (size_t)percentile + 50u) / 100u;
  return values[index];
}

static int owner_handoff_run_window(native_io_backend_kind kind, size_t warmup_hops,
                                    size_t hops, size_t window,
                                    owner_handoff_summary *out) {
  native_io_sharded *runtime = NULL;
  owner_handoff_sample warmup = {0};
  owner_handoff_sample samples[OWNER_HANDOFF_REPLICATES];
  double latency[OWNER_HANDOFF_REPLICATES];
  double rate[OWNER_HANDOFF_REPLICATES];
  uint64_t total_queued = 0u;
  uint64_t total_rejected = 0u;
  uint64_t total_direct = 0u;
  uint64_t peak = 0u;
  int status;

  status = owner_handoff_runtime_create(kind, &runtime);
  if (status != SALTS_OK) return status;

  if (warmup_hops < window) warmup_hops = window;
  status = owner_handoff_sample_run(runtime, warmup_hops, window, &warmup);
  if (status != SALTS_OK) goto cleanup;

  for (size_t replicate = 0u; replicate < OWNER_HANDOFF_REPLICATES; ++replicate) {
    status = owner_handoff_sample_run(runtime, hops, window, &samples[replicate]);
    if (status != SALTS_OK) goto cleanup;
    latency[replicate] = samples[replicate].ns_per_hop;
    rate[replicate] = samples[replicate].hops_per_second;
    total_queued += samples[replicate].queued_dispatches;
    total_rejected += samples[replicate].rejected_tasks;
    total_direct += samples[replicate].same_shard_direct_tasks;
    if (samples[replicate].peak_command_slots > peak)
      peak = samples[replicate].peak_command_slots;
  }

  out->window = window;
  out->hops = hops;
  out->replicates = OWNER_HANDOFF_REPLICATES;
  out->p50_ns_per_hop =
      owner_handoff_percentile(latency, OWNER_HANDOFF_REPLICATES, 50u);
  out->p95_ns_per_hop =
      owner_handoff_percentile(latency, OWNER_HANDOFF_REPLICATES, 95u);
  out->median_hops_per_second =
      owner_handoff_percentile(rate, OWNER_HANDOFF_REPLICATES, 50u);
  out->total_queued_dispatches = total_queued;
  out->total_rejected_tasks = total_rejected;
  out->total_same_shard_direct_tasks = total_direct;
  out->peak_command_slots = peak;

cleanup:
  {
    const int destroy_status = native_io_sharded_destroy(runtime);
    if (status == SALTS_OK) status = destroy_status;
  }
  return status;
}

static FILE *owner_handoff_open_csv(void) {
  const char *prefix = getenv("NATIVE_IO_OWNER_HANDOFF_OUTPUT");
  char path[1024];
  if (prefix == NULL || *prefix == '\0') return NULL;
  if (snprintf(path, sizeof(path), "%s.csv", prefix) < 0) return NULL;
  return fopen(path, "w");
}

static void owner_handoff_print_csv_row(FILE *stream, const char *backend,
                                        const char *cpu_set,
                                        const owner_handoff_summary *summary) {
  fprintf(stream,
          "%s,%s,%zu,%zu,%zu,%.6f,%.6f,%.6f,%" PRIu64 ",%" PRIu64
          ",%" PRIu64 ",%" PRIu64 "\n",
          backend, cpu_set, summary->window, summary->hops, summary->replicates,
          summary->p50_ns_per_hop, summary->p95_ns_per_hop,
          summary->median_hops_per_second, summary->total_queued_dispatches,
          summary->total_rejected_tasks, summary->total_same_shard_direct_tasks,
          summary->peak_command_slots);
}

int main(void) {
  const native_io_backend_kind kind = owner_handoff_backend();
  const char *backend = owner_handoff_backend_name(kind);
  const char *cpu_set = getenv("NATIVE_IO_OWNER_HANDOFF_CPU_SET");
  const size_t warmup_hops =
      owner_handoff_env_count("NATIVE_IO_OWNER_HANDOFF_WARMUP_HOPS",
                              OWNER_HANDOFF_DEFAULT_WARMUP_HOPS);
  const size_t hops =
      owner_handoff_env_count("NATIVE_IO_OWNER_HANDOFF_HOPS",
                              OWNER_HANDOFF_DEFAULT_HOPS);
  static const size_t windows[] = {1u, 2u, 4u, 8u, 16u, 32u, 64u};
  owner_handoff_summary summaries[sizeof(windows) / sizeof(windows[0])] = {{0}};
  FILE *csv;
  int status;

  if (cpu_set == NULL || *cpu_set == '\0') cpu_set = "default";
  if (kind == (native_io_backend_kind)0 || !native_io_backend_kind_supported(kind)) {
    fprintf(stderr, "unsupported NativeIO owner-handoff benchmark backend: %s\n", backend);
    return 2;
  }
  if (hops < windows[sizeof(windows) / sizeof(windows[0]) - 1u]) {
    fprintf(stderr, "owner-handoff hop count must be >= 64\n");
    return 2;
  }

  for (size_t index = 0u; index < sizeof(windows) / sizeof(windows[0]); ++index) {
    status = owner_handoff_run_window(kind, warmup_hops, hops, windows[index],
                                      &summaries[index]);
    if (status != SALTS_OK) {
      fprintf(stderr, "owner-handoff benchmark failed: window=%zu status=%d\n",
              windows[index], status);
      return 1;
    }
  }

  printf("# NativeIO owner-to-owner handoff benchmark\n\n");
  printf("Backend: %s\n\n", backend);
  printf("CPU set: %s\n\n", cpu_set);
  printf("One unmeasured external seed enters shard 0 per replicate; every reported hop is emitted by an owner callback to the other owner.\n\n");
  printf("p50/p95 are distributions of replicate batch-average ns/hop, not individual-hop latency percentiles.\n\n");
  printf("| window | hops/replicate | replicates | p50 ns/hop | p95 ns/hop | median hops/s | queued owner hops | rejected | same-owner direct | peak command slots |\n");
  printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < sizeof(windows) / sizeof(windows[0]); ++index) {
    const owner_handoff_summary *row = &summaries[index];
    printf("| %zu | %zu | %zu | %.3f | %.3f | %.0f | %" PRIu64 " | %" PRIu64
           " | %" PRIu64 " | %" PRIu64 " |\n",
           row->window, row->hops, row->replicates, row->p50_ns_per_hop,
           row->p95_ns_per_hop, row->median_hops_per_second,
           row->total_queued_dispatches, row->total_rejected_tasks,
           row->total_same_shard_direct_tasks, row->peak_command_slots);
  }

  csv = owner_handoff_open_csv();
  if (csv != NULL) {
    fprintf(csv,
            "backend,cpu_set,window,hops_per_replicate,replicates,"
            "p50_batch_ns_per_hop,p95_batch_ns_per_hop,median_hops_per_second,"
            "queued_dispatches,rejected_tasks,same_shard_direct_tasks,"
            "peak_command_slots\n");
    for (size_t index = 0u; index < sizeof(windows) / sizeof(windows[0]); ++index)
      owner_handoff_print_csv_row(csv, backend, cpu_set, &summaries[index]);
    fclose(csv);
  }

  return 0;
}
