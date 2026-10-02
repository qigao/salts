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
  OWNER_MAILBOX_DEFAULT_MESSAGES = 16384,
  OWNER_MAILBOX_DEFAULT_WARMUP_MESSAGES = 1024,
  OWNER_MAILBOX_REPLICATES = 11,
  OWNER_MAILBOX_QUEUE_CAPACITY = 1024
};

typedef struct owner_mailbox_state {
  native_io_sharded *runtime;
  native_io_sharded_task sender_task;
  native_io_sharded_task data_task;
  size_t total_messages;
  size_t window;
  size_t sent_messages;
  atomic_size_t batch_remaining;
  atomic_uint_fast64_t completed_messages;
  atomic_uint_fast64_t started_ns;
  atomic_uint_fast64_t finished_ns;
  atomic_int failure_status;
} owner_mailbox_state;

typedef struct owner_mailbox_sample {
  uint64_t wall_ns;
  double ns_per_message;
  double messages_per_second;
  uint64_t data_hops;
  uint64_t control_hops;
  uint64_t queued_dispatches;
  uint64_t rejected_tasks;
  uint64_t same_shard_direct_tasks;
  uint64_t peak_command_slots;
} owner_mailbox_sample;

typedef struct owner_mailbox_summary {
  size_t window;
  size_t messages;
  size_t replicates;
  double p50_ns_per_message;
  double p95_ns_per_message;
  double median_messages_per_second;
  uint64_t total_data_hops;
  uint64_t total_control_hops;
  uint64_t total_queued_dispatches;
  uint64_t total_rejected_tasks;
  uint64_t total_same_shard_direct_tasks;
  uint64_t peak_command_slots;
} owner_mailbox_summary;

static size_t owner_mailbox_env_count(const char *name, size_t fallback) {
  const char *value = getenv(name);
  char *end = NULL;
  unsigned long long parsed;
  if (value == NULL || *value == '\0') return fallback;
  parsed = strtoull(value, &end, 10);
  if (end == value || *end != '\0' || parsed == 0u || parsed > 10000000ull)
    return fallback;
  return (size_t)parsed;
}

static native_io_backend_kind owner_mailbox_backend(void) {
  const char *value = getenv("NATIVE_IO_OWNER_MAILBOX_BACKEND");
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

static const char *owner_mailbox_backend_name(native_io_backend_kind kind) {
  switch (kind) {
    case NATIVE_IO_BACKEND_EPOLL: return "epoll";
    case NATIVE_IO_BACKEND_IO_URING: return "io_uring";
    case NATIVE_IO_BACKEND_IOCP: return "iocp";
    case NATIVE_IO_BACKEND_KQUEUE: return "kqueue";
    default: return "unsupported";
  }
}

static void owner_mailbox_fail(owner_mailbox_state *state, int status) {
  int expected = SALTS_OK;
  if (status == SALTS_OK) status = SALTS_EIO;
  (void)atomic_compare_exchange_strong_explicit(
      &state->failure_status, &expected, status, memory_order_release, memory_order_relaxed);
}

static void owner_mailbox_sender_cancel(void *arg, int status) {
  owner_mailbox_fail((owner_mailbox_state *)arg, status);
}

static void owner_mailbox_data_cancel(void *arg, int status) {
  owner_mailbox_fail((owner_mailbox_state *)arg, status);
}

static void owner_mailbox_sender_run(native_io_sharded_context *context, void *arg) {
  owner_mailbox_state *state = (owner_mailbox_state *)arg;
  size_t batch;

  if (native_io_sharded_context_shard(context) != 0u ||
      state->sent_messages >= state->total_messages) {
    owner_mailbox_fail(state, SALTS_EPROTO);
    return;
  }

  if (atomic_load_explicit(&state->started_ns, memory_order_acquire) == 0u)
    atomic_store_explicit(&state->started_ns, salts_hrtime(), memory_order_release);

  batch = state->total_messages - state->sent_messages;
  if (batch > state->window) batch = state->window;
  atomic_store_explicit(&state->batch_remaining, batch, memory_order_release);
  state->sent_messages += batch;

  for (size_t index = 0u; index < batch; ++index) {
    const int status =
        native_io_sharded_submit_to(state->runtime, 1u, &state->data_task);
    if (status != SALTS_OK) {
      owner_mailbox_fail(state, status);
      return;
    }
  }
}

static void owner_mailbox_data_run(native_io_sharded_context *context, void *arg) {
  owner_mailbox_state *state = (owner_mailbox_state *)arg;
  const uint64_t completed =
      atomic_fetch_add_explicit(&state->completed_messages, 1u, memory_order_relaxed) + 1u;
  const size_t remaining =
      atomic_fetch_sub_explicit(&state->batch_remaining, 1u, memory_order_acq_rel);

  if (native_io_sharded_context_shard(context) != 1u || remaining == 0u) {
    owner_mailbox_fail(state, SALTS_EPROTO);
    return;
  }

  if (remaining != 1u) return;

  if (completed == state->total_messages) {
    atomic_store_explicit(&state->finished_ns, salts_hrtime(), memory_order_release);
    return;
  }
  if (completed > state->total_messages) {
    owner_mailbox_fail(state, SALTS_EPROTO);
    return;
  }

  {
    const int status =
        native_io_sharded_submit_to(state->runtime, 0u, &state->sender_task);
    if (status != SALTS_OK) owner_mailbox_fail(state, status);
  }
}

static uint64_t owner_mailbox_delta(uint64_t after, uint64_t before) {
  return after >= before ? after - before : 0u;
}

static int owner_mailbox_runtime_create(native_io_backend_kind kind,
                                        native_io_sharded **out_runtime) {
  native_io_sharded_config config = {
      2u, OWNER_MAILBOX_QUEUE_CAPACITY, {kind, 1u, 1u, 1u}};
  return native_io_sharded_create(&config, out_runtime);
}

static int owner_mailbox_sample_run(native_io_sharded *runtime, size_t messages,
                                    size_t window, owner_mailbox_sample *out) {
  owner_mailbox_state state;
  native_io_sharded_stats before = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
  native_io_sharded_stats after = NATIVE_IO_SHARDED_STATS_V1_INITIALIZER;
  uint64_t started;
  uint64_t finished;
  uint64_t queued;
  size_t batches;
  int status;

  if (window == 0u || messages < window) return SALTS_EINVAL;

  memset(&state, 0, sizeof(state));
  state.runtime = runtime;
  state.total_messages = messages;
  state.window = window;
  state.sender_task = (native_io_sharded_task){
      owner_mailbox_sender_run, owner_mailbox_sender_cancel, NULL, &state};
  state.data_task = (native_io_sharded_task){
      owner_mailbox_data_run, owner_mailbox_data_cancel, NULL, &state};
  atomic_init(&state.batch_remaining, 0u);
  atomic_init(&state.completed_messages, 0u);
  atomic_init(&state.started_ns, 0u);
  atomic_init(&state.finished_ns, 0u);
  atomic_init(&state.failure_status, SALTS_OK);

  if (!native_io_sharded_get_stats(runtime, &before)) return SALTS_EIO;
  status = native_io_sharded_submit_to(runtime, 0u, &state.sender_task);
  if (status == SALTS_OK) status = native_io_sharded_wait(runtime);
  if (status == SALTS_OK)
    status = atomic_load_explicit(&state.failure_status, memory_order_acquire);
  if (status == SALTS_OK &&
      atomic_load_explicit(&state.completed_messages, memory_order_acquire) != messages)
    status = SALTS_EPROTO;
  if (status == SALTS_OK && !native_io_sharded_get_stats(runtime, &after))
    status = SALTS_EIO;
  if (status != SALTS_OK) return status;

  started = atomic_load_explicit(&state.started_ns, memory_order_acquire);
  finished = atomic_load_explicit(&state.finished_ns, memory_order_acquire);
  if (started == 0u || finished < started) return SALTS_EPROTO;

  batches = (messages + window - 1u) / window;
  queued = owner_mailbox_delta(after.queued_dispatches, before.queued_dispatches);
  /*
   * Exactly one external seed is excluded. Each data message is one owner
   * hop to shard 1. Every non-final batch sends one control acknowledgement
   * back to shard 0 to trigger the next batch.
   */
  if (queued != UINT64_C(1) + (uint64_t)messages + (uint64_t)(batches - 1u))
    return SALTS_EPROTO;

  out->wall_ns = finished - started;
  out->ns_per_message = (double)out->wall_ns / (double)messages;
  out->messages_per_second =
      out->wall_ns == 0u ? 0.0 : (double)messages * 1.0e9 / (double)out->wall_ns;
  out->data_hops = (uint64_t)messages;
  out->control_hops = (uint64_t)(batches - 1u);
  out->queued_dispatches = queued - 1u;
  out->rejected_tasks = owner_mailbox_delta(after.rejected_tasks, before.rejected_tasks);
  out->same_shard_direct_tasks =
      owner_mailbox_delta(after.same_shard_direct_tasks, before.same_shard_direct_tasks);
  out->peak_command_slots = after.peak_command_slots;

  if (out->rejected_tasks != 0u || out->same_shard_direct_tasks != 0u)
    return SALTS_EPROTO;
  return SALTS_OK;
}

static int owner_mailbox_compare_double(const void *left, const void *right) {
  const double a = *(const double *)left;
  const double b = *(const double *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static double owner_mailbox_percentile(double *values, size_t count, unsigned percentile) {
  size_t index;
  qsort(values, count, sizeof(*values), owner_mailbox_compare_double);
  index = ((count - 1u) * (size_t)percentile + 50u) / 100u;
  return values[index];
}

static int owner_mailbox_run_window(native_io_backend_kind kind, size_t warmup_messages,
                                    size_t messages, size_t window,
                                    owner_mailbox_summary *out) {
  native_io_sharded *runtime = NULL;
  owner_mailbox_sample warmup = {0};
  owner_mailbox_sample samples[OWNER_MAILBOX_REPLICATES];
  double latency[OWNER_MAILBOX_REPLICATES];
  double rate[OWNER_MAILBOX_REPLICATES];
  uint64_t data_hops = 0u;
  uint64_t control_hops = 0u;
  uint64_t queued = 0u;
  uint64_t rejected = 0u;
  uint64_t direct = 0u;
  uint64_t peak = 0u;
  int status;

  status = owner_mailbox_runtime_create(kind, &runtime);
  if (status != SALTS_OK) return status;

  if (warmup_messages < window) warmup_messages = window;
  status = owner_mailbox_sample_run(runtime, warmup_messages, window, &warmup);
  if (status != SALTS_OK) goto cleanup;

  for (size_t replicate = 0u; replicate < OWNER_MAILBOX_REPLICATES; ++replicate) {
    status = owner_mailbox_sample_run(runtime, messages, window, &samples[replicate]);
    if (status != SALTS_OK) goto cleanup;
    latency[replicate] = samples[replicate].ns_per_message;
    rate[replicate] = samples[replicate].messages_per_second;
    data_hops += samples[replicate].data_hops;
    control_hops += samples[replicate].control_hops;
    queued += samples[replicate].queued_dispatches;
    rejected += samples[replicate].rejected_tasks;
    direct += samples[replicate].same_shard_direct_tasks;
    if (samples[replicate].peak_command_slots > peak)
      peak = samples[replicate].peak_command_slots;
  }

  out->window = window;
  out->messages = messages;
  out->replicates = OWNER_MAILBOX_REPLICATES;
  out->p50_ns_per_message =
      owner_mailbox_percentile(latency, OWNER_MAILBOX_REPLICATES, 50u);
  out->p95_ns_per_message =
      owner_mailbox_percentile(latency, OWNER_MAILBOX_REPLICATES, 95u);
  out->median_messages_per_second =
      owner_mailbox_percentile(rate, OWNER_MAILBOX_REPLICATES, 50u);
  out->total_data_hops = data_hops;
  out->total_control_hops = control_hops;
  out->total_queued_dispatches = queued;
  out->total_rejected_tasks = rejected;
  out->total_same_shard_direct_tasks = direct;
  out->peak_command_slots = peak;

cleanup:
  {
    const int destroy_status = native_io_sharded_destroy(runtime);
    if (status == SALTS_OK) status = destroy_status;
  }
  return status;
}

static FILE *owner_mailbox_open_csv(void) {
  const char *prefix = getenv("NATIVE_IO_OWNER_MAILBOX_OUTPUT");
  char path[1024];
  if (prefix == NULL || *prefix == '\0') return NULL;
  if (snprintf(path, sizeof(path), "%s.csv", prefix) < 0) return NULL;
  return fopen(path, "w");
}

static void owner_mailbox_print_csv_row(FILE *stream, const char *backend,
                                        const char *cpu_set,
                                        const owner_mailbox_summary *summary) {
  fprintf(stream,
          "%s,%s,%zu,%zu,%zu,%.6f,%.6f,%.6f,%" PRIu64 ",%" PRIu64
          ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
          backend, cpu_set, summary->window, summary->messages, summary->replicates,
          summary->p50_ns_per_message, summary->p95_ns_per_message,
          summary->median_messages_per_second, summary->total_data_hops,
          summary->total_control_hops, summary->total_queued_dispatches,
          summary->total_rejected_tasks, summary->total_same_shard_direct_tasks,
          summary->peak_command_slots);
}

int main(void) {
  const native_io_backend_kind kind = owner_mailbox_backend();
  const char *backend = owner_mailbox_backend_name(kind);
  const char *cpu_set = getenv("NATIVE_IO_OWNER_MAILBOX_CPU_SET");
  const size_t warmup_messages =
      owner_mailbox_env_count("NATIVE_IO_OWNER_MAILBOX_WARMUP_MESSAGES",
                              OWNER_MAILBOX_DEFAULT_WARMUP_MESSAGES);
  const size_t messages =
      owner_mailbox_env_count("NATIVE_IO_OWNER_MAILBOX_MESSAGES",
                              OWNER_MAILBOX_DEFAULT_MESSAGES);
  static const size_t full_windows[] = {
      1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u, 256u, 512u, 1024u};
  static const size_t pr_windows[] = {1u, 16u, 64u, 256u};
  const char *profile = getenv("SALTS_BENCH_PROFILE");
  const int pr_profile = profile != NULL && strcmp(profile, "pr") == 0;
  const size_t *windows = pr_profile ? pr_windows : full_windows;
  const size_t window_count =
      pr_profile ? sizeof(pr_windows) / sizeof(pr_windows[0])
                 : sizeof(full_windows) / sizeof(full_windows[0]);
  owner_mailbox_summary summaries[sizeof(full_windows) / sizeof(full_windows[0])] = {{0}};
  FILE *csv;
  int status;

  if (cpu_set == NULL || *cpu_set == '\0') cpu_set = "default";
  if (kind == (native_io_backend_kind)0 || !native_io_backend_kind_supported(kind)) {
    fprintf(stderr, "unsupported NativeIO owner-mailbox benchmark backend: %s\n", backend);
    return 2;
  }
  if (messages < windows[window_count - 1u]) {
    fprintf(stderr, "owner-mailbox message count must be >= %zu\n",
            windows[window_count - 1u]);
    return 2;
  }

  for (size_t index = 0u; index < window_count; ++index) {
    status = owner_mailbox_run_window(kind, warmup_messages, messages, windows[index],
                                      &summaries[index]);
    if (status != SALTS_OK) {
      fprintf(stderr, "owner-mailbox benchmark failed: window=%zu status=%d\n",
              windows[index], status);
      return 1;
    }
  }

  printf("# NativeIO owner one-way mailbox benchmark\n\n");
  printf("Backend: %s\n\n", backend);
  printf("CPU set: %s\n\n", cpu_set);
  printf("Each window sends data from shard 0 to shard 1 and uses one reverse control hop to trigger the next window; the final window needs no acknowledgement.\n\n");
  printf("p50/p95 are distributions of replicate batch-average ns/data-message, not individual-message latency percentiles.\n\n");
  printf("| window | messages/replicate | replicates | p50 ns/message | p95 ns/message | median messages/s | data hops | control hops | queued dispatches | rejected | same-owner direct | peak command slots |\n");
  printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < window_count; ++index) {
    const owner_mailbox_summary *row = &summaries[index];
    printf("| %zu | %zu | %zu | %.3f | %.3f | %.0f | %" PRIu64 " | %" PRIu64
           " | %" PRIu64 " | %" PRIu64 " | %" PRIu64 " | %" PRIu64 " |\n",
           row->window, row->messages, row->replicates, row->p50_ns_per_message,
           row->p95_ns_per_message, row->median_messages_per_second,
           row->total_data_hops, row->total_control_hops,
           row->total_queued_dispatches, row->total_rejected_tasks,
           row->total_same_shard_direct_tasks, row->peak_command_slots);
  }

  csv = owner_mailbox_open_csv();
  if (csv != NULL) {
    fprintf(csv,
            "backend,cpu_set,window,messages_per_replicate,replicates,"
            "p50_batch_ns_per_message,p95_batch_ns_per_message,"
            "median_messages_per_second,data_hops,control_hops,queued_dispatches,"
            "rejected_tasks,same_shard_direct_tasks,peak_command_slots\n");
    for (size_t index = 0u; index < window_count; ++index)
      owner_mailbox_print_csv_row(csv, backend, cpu_set, &summaries[index]);
    fclose(csv);
  }

  return 0;
}
