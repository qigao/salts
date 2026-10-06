#include "coro_executor_internal.h"

#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/thread.h>

#include <inttypes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  DEQUEUE_BENCH_QUEUE_CAPACITY = 8192,
  DEQUEUE_BENCH_POOL_CAPACITY = 128,
  DEQUEUE_BENCH_LOW_TASKS = 4096,
  DEQUEUE_BENCH_NEAR_CAPACITY_TASKS = 8192,
  DEQUEUE_BENCH_REPLICATES = 11,
  DEQUEUE_BENCH_WAIT_ROUNDS = 4000
};

typedef struct dequeue_bench_gate {
  atomic_int started;
  atomic_int release;
} dequeue_bench_gate;

typedef struct dequeue_bench_state {
  atomic_size_t next;
  atomic_uint_fast64_t runs;
  atomic_uint_fast64_t order_errors;
} dequeue_bench_state;

typedef struct dequeue_bench_task_arg {
  dequeue_bench_state *state;
  size_t expected;
} dequeue_bench_task_arg;

typedef struct dequeue_bench_summary {
  const char *style;
  const char *occupancy;
  size_t batch_size;
  size_t tasks;
  size_t replicates;
  double p50_ns_per_task;
  double p95_ns_per_task;
  double median_tasks_per_second;
  double dequeue_locks_per_task;
  double queue_space_broadcasts_per_task;
  uint64_t submitted_tasks;
  uint64_t completed_tasks;
  uint64_t rejected_tasks;
  uint64_t order_errors;
} dequeue_bench_summary;

static void dequeue_bench_gate_task(coro_t *coroutine, void *arg) {
  dequeue_bench_gate *gate = (dequeue_bench_gate *)arg;
  atomic_fetch_add_explicit(&gate->started, 1, memory_order_release);
  while (atomic_load_explicit(&gate->release, memory_order_acquire) == 0)
    coro_yield();
  (void)coroutine;
}

static void dequeue_bench_task(coro_t *coroutine, void *arg) {
  dequeue_bench_task_arg *task = (dequeue_bench_task_arg *)arg;
  const size_t seen =
      atomic_fetch_add_explicit(&task->state->next, 1u, memory_order_relaxed);
  (void)coroutine;
  if (seen != task->expected)
    atomic_fetch_add_explicit(&task->state->order_errors, 1u, memory_order_relaxed);
  atomic_fetch_add_explicit(&task->state->runs, 1u, memory_order_release);
}

static int dequeue_bench_wait_started(dequeue_bench_gate *gate) {
  for (int round = 0; round < DEQUEUE_BENCH_WAIT_ROUNDS; ++round) {
    if (atomic_load_explicit(&gate->started, memory_order_acquire) ==
        DEQUEUE_BENCH_POOL_CAPACITY)
      return 1;
    cmeta_sleep_ms(1u);
  }
  return 0;
}

static coro_executor_t *dequeue_bench_create_executor(size_t batch_size) {
  coro_executor_config_t config = CORO_EXECUTOR_CONFIG_DEFAULT;
  coro_executor_t *executor;

  config.worker_count = 1u;
  config.queue_capacity_per_worker = DEQUEUE_BENCH_QUEUE_CAPACITY;
  config.coroutine_pool.initial_capacity = 0u;
  config.coroutine_pool.max_capacity = DEQUEUE_BENCH_POOL_CAPACITY;
  executor = coro_executor_create(&config);
  if (executor == NULL) return NULL;
  if (coro_executor_set_dequeue_batch_limit_internal(
          executor, 0u, batch_size) != SALTS_OK) {
    (void)coro_executor_destroy(executor);
    return NULL;
  }
  return executor;
}

static int dequeue_bench_compare_double(const void *left, const void *right) {
  const double a = *(const double *)left;
  const double b = *(const double *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static double dequeue_bench_percentile(double *values, size_t count,
                                       unsigned percentile) {
  const size_t index =
      ((count - 1u) * (size_t)percentile + 50u) / 100u;
  qsort(values, count, sizeof(*values), dequeue_bench_compare_double);
  return values[index];
}

static int dequeue_bench_run_case(const char *style, const char *occupancy,
                                  size_t batch_size,
                                  dequeue_bench_summary *out) {
  const int use_batch = strcmp(style, "batch_take") == 0;
  const size_t tasks_count =
      strcmp(occupancy, "near_capacity") == 0
          ? DEQUEUE_BENCH_NEAR_CAPACITY_TASKS
          : DEQUEUE_BENCH_LOW_TASKS;
  coro_executor_t *executor = NULL;
  dequeue_bench_task_arg *args = NULL;
  coro_executor_task_t *tasks = NULL;
  double latency[DEQUEUE_BENCH_REPLICATES];
  double rate[DEQUEUE_BENCH_REPLICATES];
  uint64_t submitted = 0u;
  uint64_t completed = 0u;
  uint64_t rejected = 0u;
  uint64_t order_errors = 0u;
  int status = SALTS_OK;

  if (out == NULL || batch_size == 0u ||
      batch_size > CORO_EXECUTOR_INTERNAL_MAX_DEQUEUE_BATCH)
    return SALTS_EINVAL;

  executor = dequeue_bench_create_executor(use_batch ? batch_size : 1u);
  args = (dequeue_bench_task_arg *)calloc(tasks_count, sizeof(*args));
  tasks = (coro_executor_task_t *)calloc(tasks_count, sizeof(*tasks));
  if (executor == NULL || args == NULL || tasks == NULL) {
    if (executor != NULL) (void)coro_executor_destroy(executor);
    free(tasks);
    free(args);
    return SALTS_ENOMEM;
  }

  for (size_t replicate = 0u;
       replicate < DEQUEUE_BENCH_REPLICATES; ++replicate) {
    dequeue_bench_gate gate;
    dequeue_bench_state state;
    coro_executor_task_t
        gate_tasks[DEQUEUE_BENCH_POOL_CAPACITY];
    coro_executor_stats_t before = {0};
    coro_executor_stats_t after = {0};
    uint64_t started;
    uint64_t elapsed;

    atomic_init(&gate.started, 0);
    atomic_init(&gate.release, 0);
    atomic_init(&state.next, 0u);
    atomic_init(&state.runs, 0u);
    atomic_init(&state.order_errors, 0u);

    coro_executor_get_stats(executor, &before);
    for (size_t index = 0u;
         index < DEQUEUE_BENCH_POOL_CAPACITY; ++index)
      gate_tasks[index] = (coro_executor_task_t){
          dequeue_bench_gate_task, NULL, NULL, &gate};

    status = coro_executor_try_submit_batch_to_internal(
        executor, 0u, gate_tasks, DEQUEUE_BENCH_POOL_CAPACITY);
    if (status != SALTS_OK || !dequeue_bench_wait_started(&gate)) {
      if (status == SALTS_OK) status = SALTS_ETIMEDOUT;
      atomic_store_explicit(&gate.release, 1, memory_order_release);
      (void)coro_executor_wait(executor);
      break;
    }

    for (size_t index = 0u; index < tasks_count; ++index) {
      args[index] = (dequeue_bench_task_arg){&state, index};
      tasks[index] = (coro_executor_task_t){
          dequeue_bench_task, NULL, NULL, &args[index]};
    }

    status = coro_executor_try_submit_batch_to_internal(
        executor, 0u, tasks, tasks_count);
    if (status != SALTS_OK) {
      atomic_store_explicit(&gate.release, 1, memory_order_release);
      (void)coro_executor_wait(executor);
      break;
    }

    started = cmeta_hrtime();
    atomic_store_explicit(&gate.release, 1, memory_order_release);
    status = coro_executor_wait(executor);
    elapsed = cmeta_hrtime() - started;
    coro_executor_get_stats(executor, &after);

    if (status == SALTS_OK &&
        (atomic_load_explicit(&state.runs, memory_order_acquire) !=
             (uint64_t)tasks_count ||
         atomic_load_explicit(&state.next, memory_order_acquire) !=
             tasks_count ||
         atomic_load_explicit(&state.order_errors, memory_order_acquire) !=
             0u ||
         after.submitted_tasks - before.submitted_tasks !=
             (uint64_t)(DEQUEUE_BENCH_POOL_CAPACITY + tasks_count) ||
         after.completed_tasks - before.completed_tasks !=
             after.submitted_tasks - before.submitted_tasks ||
         after.cancelled_tasks != before.cancelled_tasks ||
         after.rejected_tasks != before.rejected_tasks))
      status = SALTS_EPROTO;

    if (status != SALTS_OK) break;

    latency[replicate] = (double)elapsed / (double)tasks_count;
    rate[replicate] =
        elapsed == 0u
            ? 0.0
            : (double)tasks_count * 1.0e9 / (double)elapsed;
    submitted += after.submitted_tasks - before.submitted_tasks;
    completed += after.completed_tasks - before.completed_tasks;
    rejected += after.rejected_tasks - before.rejected_tasks;
    order_errors +=
        atomic_load_explicit(&state.order_errors, memory_order_acquire);
  }

  if (status == SALTS_OK) {
    out->style = style;
    out->occupancy = occupancy;
    out->batch_size = batch_size;
    out->tasks = tasks_count;
    out->replicates = DEQUEUE_BENCH_REPLICATES;
    out->p50_ns_per_task =
        dequeue_bench_percentile(
            latency, DEQUEUE_BENCH_REPLICATES, 50u);
    out->p95_ns_per_task =
        dequeue_bench_percentile(
            latency, DEQUEUE_BENCH_REPLICATES, 95u);
    out->median_tasks_per_second =
        dequeue_bench_percentile(
            rate, DEQUEUE_BENCH_REPLICATES, 50u);
    out->dequeue_locks_per_task =
        use_batch ? 1.0 / (double)batch_size : 1.0;
    out->queue_space_broadcasts_per_task =
        use_batch ? 1.0 / (double)batch_size : 1.0;
    out->submitted_tasks = submitted;
    out->completed_tasks = completed;
    out->rejected_tasks = rejected;
    out->order_errors = order_errors;
  }

  {
    const int destroy_status = coro_executor_destroy(executor);
    if (status == SALTS_OK) status = destroy_status;
  }
  free(tasks);
  free(args);
  return status;
}

static FILE *dequeue_bench_open_csv(void) {
  const char *prefix =
      getenv("CORO_EXECUTOR_DEQUEUE_BATCH_BENCH_OUTPUT");
  char path[1024];
  if (prefix == NULL || *prefix == '\0') return NULL;
  if (snprintf(path, sizeof(path), "%s.csv", prefix) < 0) return NULL;
  return fopen(path, "w");
}

static void dequeue_bench_print_csv(
    FILE *csv, const dequeue_bench_summary *row) {
  fprintf(
      csv,
      "%s,%s,%zu,%zu,%zu,%.6f,%.6f,%.6f,%.9f,%.9f,"
      "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
      row->style, row->occupancy, row->batch_size, row->tasks,
      row->replicates, row->p50_ns_per_task, row->p95_ns_per_task,
      row->median_tasks_per_second, row->dequeue_locks_per_task,
      row->queue_space_broadcasts_per_task, row->submitted_tasks,
      row->completed_tasks, row->rejected_tasks, row->order_errors);
}

int main(void) {
  static const size_t full_sizes[] = {2u, 4u, 8u, 16u, 32u, 64u, 128u};
  static const size_t pr_sizes[] = {2u, 16u, 128u};
  static const char *styles[] = {"per_item_take", "batch_take"};
  static const char *occupancies[] = {"low", "near_capacity"};
  const char *profile = getenv("SALTS_BENCH_PROFILE");
  const int pr_profile = profile != NULL && strcmp(profile, "pr") == 0;
  const size_t *sizes = pr_profile ? pr_sizes : full_sizes;
  const size_t size_count =
      pr_profile ? sizeof(pr_sizes) / sizeof(pr_sizes[0])
                 : sizeof(full_sizes) / sizeof(full_sizes[0]);
  dequeue_bench_summary rows[
      (sizeof(full_sizes) / sizeof(full_sizes[0])) *
      (sizeof(styles) / sizeof(styles[0])) *
      (sizeof(occupancies) / sizeof(occupancies[0]))];
  size_t row_count = 0u;
  FILE *csv;

  for (size_t occupancy = 0u;
       occupancy < sizeof(occupancies) / sizeof(occupancies[0]);
       ++occupancy) {
    for (size_t size = 0u; size < size_count; ++size) {
      for (size_t style = 0u;
           style < sizeof(styles) / sizeof(styles[0]); ++style) {
        const int status = dequeue_bench_run_case(
            styles[style], occupancies[occupancy], sizes[size],
            &rows[row_count]);
        if (status != SALTS_OK) {
          fprintf(
              stderr,
              "Coroutine Executor dequeue batch benchmark failed: "
              "style=%s occupancy=%s batch=%zu status=%d\n",
              styles[style], occupancies[occupancy], sizes[size],
              status);
          return 1;
        }
        ++row_count;
      }
    }
  }

  printf("# Coroutine Executor consumer-side dequeue batching POC\n\n");
  printf(
      "Producer admission is completed before timing with the same private "
      "range submit path for both styles. 128 yielding gate coroutines fill "
      "the pool so measured tasks remain queued until the timed gate release.\n\n");
  printf(
      "| style | occupancy | batch | tasks | replicates | p50 ns/task | "
      "p95 ns/task | median tasks/s | dequeue locks/task | "
      "queue-space broadcasts/task | submitted | completed | rejected | "
      "order errors |\n");
  printf(
      "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | "
      "---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < row_count; ++index) {
    const dequeue_bench_summary *row = &rows[index];
    printf(
        "| %s | %s | %zu | %zu | %zu | %.3f | %.3f | %.0f | %.6f | "
        "%.6f | %" PRIu64 " | %" PRIu64 " | %" PRIu64 " | %" PRIu64
        " |\n",
        row->style, row->occupancy, row->batch_size, row->tasks,
        row->replicates, row->p50_ns_per_task, row->p95_ns_per_task,
        row->median_tasks_per_second, row->dequeue_locks_per_task,
        row->queue_space_broadcasts_per_task, row->submitted_tasks,
        row->completed_tasks, row->rejected_tasks, row->order_errors);
  }

  csv = dequeue_bench_open_csv();
  if (csv != NULL) {
    fprintf(
        csv,
        "style,occupancy,batch_size,tasks_per_replicate,replicates,"
        "p50_ns_per_task,p95_ns_per_task,median_tasks_per_second,"
        "dequeue_locks_per_task,queue_space_broadcasts_per_task,"
        "submitted_tasks,completed_tasks,rejected_tasks,order_errors\n");
    for (size_t index = 0u; index < row_count; ++index)
      dequeue_bench_print_csv(csv, &rows[index]);
    fclose(csv);
  }
  return 0;
}
