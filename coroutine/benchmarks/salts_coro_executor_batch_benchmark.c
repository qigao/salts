#include "salts_coro_executor_internal.h"

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
  BATCH_BENCH_QUEUE_CAPACITY = 2048,
  BATCH_BENCH_REPLICATES = 11,
  BATCH_BENCH_MAX_BATCH = 128,
  BATCH_BENCH_WAIT_ROUNDS = 2000
};

typedef struct batch_bench_gate {
  atomic_int started;
  atomic_int release;
} batch_bench_gate;

typedef struct batch_bench_task_state {
  atomic_uint_fast64_t runs;
} batch_bench_task_state;

typedef struct batch_bench_summary {
  const char *style;
  const char *occupancy;
  size_t batch_size;
  size_t replicates;
  double p50_ns_per_task;
  double p95_ns_per_task;
  double median_tasks_per_second;
  double admission_locks_per_task;
  double wake_signals_per_task;
  uint64_t submitted_tasks;
  uint64_t rejected_tasks;
} batch_bench_summary;

static void batch_bench_gate_task(coro_t *coroutine, void *arg) {
  batch_bench_gate *gate = (batch_bench_gate *)arg;
  (void)coroutine;
  atomic_store_explicit(&gate->started, 1, memory_order_release);
  while (atomic_load_explicit(&gate->release, memory_order_acquire) == 0)
    (void)coro_yield();
}

static void batch_bench_noop_task(coro_t *coroutine, void *arg) {
  batch_bench_task_state *state = (batch_bench_task_state *)arg;
  (void)coroutine;
  atomic_fetch_add_explicit(&state->runs, 1u, memory_order_relaxed);
}

static int batch_bench_wait_started(batch_bench_gate *gate) {
  for (int round = 0; round < BATCH_BENCH_WAIT_ROUNDS; ++round) {
    if (atomic_load_explicit(&gate->started, memory_order_acquire) != 0) return 1;
    salts_sleep_ms(1u);
  }
  return 0;
}

static salts_coro_executor_t *batch_bench_create_executor(void) {
  salts_coro_executor_config_t config = SALTS_CORO_EXECUTOR_CONFIG_DEFAULT;
  config.worker_count = 1u;
  config.queue_capacity_per_worker = BATCH_BENCH_QUEUE_CAPACITY;
  config.coroutine_pool.initial_capacity = 0u;
  config.coroutine_pool.max_capacity = 1u;
  return salts_coro_executor_create(&config);
}

static int batch_bench_compare_double(const void *left, const void *right) {
  const double a = *(const double *)left;
  const double b = *(const double *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static double batch_bench_percentile(double *values, size_t count, unsigned percentile) {
  const size_t index = ((count - 1u) * (size_t)percentile + 50u) / 100u;
  qsort(values, count, sizeof(*values), batch_bench_compare_double);
  return values[index];
}

static int batch_bench_prefill(salts_coro_executor_t *executor,
                               const salts_coro_executor_task_t *task,
                               size_t count) {
  for (size_t index = 0u; index < count; ++index) {
    const int status = salts_coro_executor_try_submit_to(executor, 0u, task);
    if (status != SALTS_OK) return status;
  }
  return SALTS_OK;
}

static int batch_bench_run_case(const char *style, const char *occupancy,
                                size_t batch_size, batch_bench_summary *out) {
  salts_coro_executor_t *executor = batch_bench_create_executor();
  batch_bench_task_state task_state;
  salts_coro_executor_task_t tasks[BATCH_BENCH_MAX_BATCH];
  double latency[BATCH_BENCH_REPLICATES];
  double rate[BATCH_BENCH_REPLICATES];
  uint64_t submitted = 0u;
  uint64_t rejected = 0u;
  const int use_batch = strcmp(style, "batch") == 0;
  const int near_capacity = strcmp(occupancy, "near_capacity") == 0;
  int status = SALTS_OK;

  if (executor == NULL || out == NULL || batch_size == 0u ||
      batch_size > BATCH_BENCH_MAX_BATCH) {
    if (executor != NULL) (void)salts_coro_executor_destroy(executor);
    return SALTS_EINVAL;
  }

  atomic_init(&task_state.runs, 0u);
  for (size_t index = 0u; index < batch_size; ++index)
    tasks[index] = (salts_coro_executor_task_t){
        batch_bench_noop_task, NULL, NULL, &task_state};

  for (size_t replicate = 0u; replicate < BATCH_BENCH_REPLICATES; ++replicate) {
    batch_bench_gate gate;
    salts_coro_executor_task_t gate_task;
    salts_coro_executor_stats_t before = {0};
    salts_coro_executor_stats_t after = {0};
    size_t prefill = 0u;
    uint64_t started;
    uint64_t elapsed;

    atomic_init(&gate.started, 0);
    atomic_init(&gate.release, 0);
    gate_task = (salts_coro_executor_task_t){
        batch_bench_gate_task, NULL, NULL, &gate};

    status = salts_coro_executor_submit_to(executor, 0u, &gate_task);
    if (status != SALTS_OK) break;
    if (!batch_bench_wait_started(&gate)) {
      status = SALTS_ETIMEDOUT;
      break;
    }

    if (near_capacity)
      prefill = BATCH_BENCH_QUEUE_CAPACITY - batch_size;
    status = batch_bench_prefill(executor, &tasks[0], prefill);
    if (status != SALTS_OK) {
      atomic_store_explicit(&gate.release, 1, memory_order_release);
      (void)salts_coro_executor_wait(executor);
      break;
    }

    salts_coro_executor_get_stats(executor, &before);
    started = salts_hrtime();
    if (use_batch) {
      status = salts_coro_executor_try_submit_batch_to_internal(
          executor, 0u, tasks, batch_size);
    } else {
      for (size_t index = 0u; index < batch_size && status == SALTS_OK; ++index)
        status = salts_coro_executor_try_submit_to(executor, 0u, &tasks[index]);
    }
    elapsed = salts_hrtime() - started;
    salts_coro_executor_get_stats(executor, &after);

    atomic_store_explicit(&gate.release, 1, memory_order_release);
    if (status == SALTS_OK) status = salts_coro_executor_wait(executor);
    if (status != SALTS_OK) break;

    if (after.submitted_tasks - before.submitted_tasks != (uint64_t)batch_size ||
        after.rejected_tasks != before.rejected_tasks) {
      status = SALTS_EPROTO;
      break;
    }

    latency[replicate] = (double)elapsed / (double)batch_size;
    rate[replicate] =
        elapsed == 0u ? 0.0 : (double)batch_size * 1.0e9 / (double)elapsed;
    submitted += after.submitted_tasks - before.submitted_tasks;
    rejected += after.rejected_tasks - before.rejected_tasks;
  }

  if (status == SALTS_OK) {
    out->style = style;
    out->occupancy = occupancy;
    out->batch_size = batch_size;
    out->replicates = BATCH_BENCH_REPLICATES;
    out->p50_ns_per_task =
        batch_bench_percentile(latency, BATCH_BENCH_REPLICATES, 50u);
    out->p95_ns_per_task =
        batch_bench_percentile(latency, BATCH_BENCH_REPLICATES, 95u);
    out->median_tasks_per_second =
        batch_bench_percentile(rate, BATCH_BENCH_REPLICATES, 50u);
    out->admission_locks_per_task = use_batch ? 1.0 / (double)batch_size : 1.0;
    out->wake_signals_per_task = use_batch ? 1.0 / (double)batch_size : 1.0;
    out->submitted_tasks = submitted;
    out->rejected_tasks = rejected;
  }

  {
    const int destroy_status = salts_coro_executor_destroy(executor);
    if (status == SALTS_OK) status = destroy_status;
  }
  return status;
}

static FILE *batch_bench_open_csv(void) {
  const char *prefix = getenv("SALTS_CORO_EXECUTOR_BATCH_BENCH_OUTPUT");
  char path[1024];
  if (prefix == NULL || *prefix == '\0') return NULL;
  if (snprintf(path, sizeof(path), "%s.csv", prefix) < 0) return NULL;
  return fopen(path, "w");
}

static void batch_bench_print_csv(FILE *csv, const batch_bench_summary *row) {
  fprintf(csv, "%s,%s,%zu,%zu,%.6f,%.6f,%.6f,%.9f,%.9f,%" PRIu64 ",%" PRIu64 "\n",
          row->style, row->occupancy, row->batch_size, row->replicates,
          row->p50_ns_per_task, row->p95_ns_per_task,
          row->median_tasks_per_second, row->admission_locks_per_task,
          row->wake_signals_per_task, row->submitted_tasks, row->rejected_tasks);
}

int main(void) {
  static const size_t sizes[] = {2u, 4u, 8u, 16u, 32u, 64u, 128u};
  static const char *styles[] = {"per_item", "batch"};
  static const char *occupancies[] = {"low", "near_capacity"};
  batch_bench_summary rows[
      (sizeof(sizes) / sizeof(sizes[0])) *
      (sizeof(styles) / sizeof(styles[0])) *
      (sizeof(occupancies) / sizeof(occupancies[0]))];
  size_t row_count = 0u;
  FILE *csv;

  for (size_t occupancy = 0u;
       occupancy < sizeof(occupancies) / sizeof(occupancies[0]); ++occupancy) {
    for (size_t size = 0u; size < sizeof(sizes) / sizeof(sizes[0]); ++size) {
      for (size_t style = 0u; style < sizeof(styles) / sizeof(styles[0]); ++style) {
        const int status = batch_bench_run_case(
            styles[style], occupancies[occupancy], sizes[size], &rows[row_count]);
        if (status != SALTS_OK) {
          fprintf(stderr,
                  "Coroutine Executor batch benchmark failed: style=%s occupancy=%s batch=%zu status=%d\n",
                  styles[style], occupancies[occupancy], sizes[size], status);
          return 1;
        }
        ++row_count;
      }
    }
  }

  printf("# Coroutine Executor producer-side batch admission POC\n\n");
  printf("Worker dequeue remains per-item. Timed admission runs while one gate coroutine keeps the worker from consuming the task queue.\n\n");
  printf("| style | occupancy | batch | replicates | p50 ns/task | p95 ns/task | median admission tasks/s | locks/task | signals/task | submitted | rejected |\n");
  printf("| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < row_count; ++index) {
    const batch_bench_summary *row = &rows[index];
    printf("| %s | %s | %zu | %zu | %.3f | %.3f | %.0f | %.6f | %.6f | %" PRIu64 " | %" PRIu64 " |\n",
           row->style, row->occupancy, row->batch_size, row->replicates,
           row->p50_ns_per_task, row->p95_ns_per_task,
           row->median_tasks_per_second, row->admission_locks_per_task,
           row->wake_signals_per_task, row->submitted_tasks, row->rejected_tasks);
  }

  csv = batch_bench_open_csv();
  if (csv != NULL) {
    fprintf(csv,
            "style,occupancy,batch_size,replicates,p50_admission_ns_per_task,"
            "p95_admission_ns_per_task,median_admission_tasks_per_second,"
            "admission_locks_per_task,wake_signals_per_task,submitted_tasks,rejected_tasks\n");
    for (size_t index = 0u; index < row_count; ++index)
      batch_bench_print_csv(csv, &rows[index]);
    fclose(csv);
  }
  return 0;
}
