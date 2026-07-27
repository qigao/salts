#include "data_bind.h"
#include "tinytest.h"
#include "turbo_thread.h"

#include <stdio.h>
#include <stdatomic.h>
#include <string.h>

#define DATA_BIND_POOL_BENCH_ITERS 10000U
#define DATA_BIND_POOL_BENCH_ITEMS 80U
#define DATA_BIND_POOL_BENCH_MAX_THREADS 8U
#define DATA_BIND_POOL_BENCH_PARALLEL_ITERS 1000U
#define DATA_BIND_POOL_BENCH_PARALLEL_REPEATS 5U

typedef struct data_bind_pool_worker {
  DataBind *codec;
  atomic_size_t *ready_count;
  atomic_int *start;
  size_t iterations;
} data_bind_pool_worker_t;

static DataBind *g_pool_bench_codecs[DATA_BIND_POOL_BENCH_MAX_THREADS];
static char g_pool_bench_json[512];
static atomic_size_t g_pool_bench_sink;
static atomic_size_t g_pool_bench_failures;

static void data_bind_pool_bench_worker(void *arg) {
  data_bind_pool_worker_t *worker = (data_bind_pool_worker_t *)arg;
  size_t i;
  atomic_fetch_add_explicit(worker->ready_count, 1U, memory_order_release);
  while (!atomic_load_explicit(worker->start, memory_order_acquire)) turbo_thread_yield();
  for (i = 0; i < worker->iterations; ++i) {
    DataBindValue *value = NULL;
    if (data_bind_parse_json(worker->codec, "Batch", g_pool_bench_json,
                             strlen(g_pool_bench_json), &value, NULL) != DATA_BIND_OK) {
      atomic_fetch_add_explicit(&g_pool_bench_failures, 1U, memory_order_relaxed);
      continue;
    }
    atomic_fetch_add_explicit(&g_pool_bench_sink, data_bind_value_count(value),
                              memory_order_relaxed);
    data_bind_value_free(value);
  }
}

static void data_bind_pool_bench_parallel(size_t thread_count) {
  turbo_thread_t threads[DATA_BIND_POOL_BENCH_MAX_THREADS] = {0};
  data_bind_pool_worker_t workers[DATA_BIND_POOL_BENCH_MAX_THREADS];
  atomic_size_t ready_count = 0;
  atomic_int start = 0;
  size_t created = 0;
  size_t i;

  for (i = 0; i < thread_count; ++i) {
    workers[i].codec = g_pool_bench_codecs[i];
    workers[i].ready_count = &ready_count;
    workers[i].start = &start;
    workers[i].iterations = DATA_BIND_POOL_BENCH_PARALLEL_ITERS;
    if (turbo_thread_create(&threads[i], data_bind_pool_bench_worker, &workers[i]) != 0) {
      atomic_fetch_add_explicit(&g_pool_bench_failures, 1U, memory_order_relaxed);
      break;
    }
    created++;
  }
  while (atomic_load_explicit(&ready_count, memory_order_acquire) < created) turbo_thread_yield();
  atomic_store_explicit(&start, 1, memory_order_release);
  for (i = 0; i < created; ++i) {
    if (turbo_thread_join(&threads[i]) != 0)
      atomic_fetch_add_explicit(&g_pool_bench_failures, 1U, memory_order_relaxed);
  }
}

static void data_bind_pool_bench_prepare(void) {
  FILE *file = fopen("benchmark_data_bind_pool.tbe", "w");
  size_t offset = 0;
  size_t i;
  int written;
  size_t codec_index;
  check_not_null(file);
  if (file != NULL) {
    fputs("message Batch { uint32[80] values; }\n", file);
    fclose(file);
  }

  written = snprintf(g_pool_bench_json, sizeof(g_pool_bench_json), "{\"values\":[");
  check_true(written > 0);
  if (written <= 0) return;
  offset = (size_t)written;
  for (i = 0; i < DATA_BIND_POOL_BENCH_ITEMS; ++i) {
    written = snprintf(g_pool_bench_json + offset, sizeof(g_pool_bench_json) - offset,
                       "%s%u", i == 0 ? "" : ",", (unsigned)i);
    check_true(written > 0 && (size_t)written < sizeof(g_pool_bench_json) - offset);
    if (written <= 0 || (size_t)written >= sizeof(g_pool_bench_json) - offset) return;
    offset += (size_t)written;
  }
  (void)snprintf(g_pool_bench_json + offset, sizeof(g_pool_bench_json) - offset, "]}");

  for (codec_index = 0; codec_index < DATA_BIND_POOL_BENCH_MAX_THREADS; ++codec_index) {
    check_int_eq(data_bind_create("benchmark_data_bind_pool.tbe",
                                  &g_pool_bench_codecs[codec_index], NULL),
                 DATA_BIND_OK);
    check_not_null(g_pool_bench_codecs[codec_index]);
  }
}

spec("DataBind value pool benchmarks") {
  before_all() { data_bind_pool_bench_prepare(); }

  after_all() {
    size_t i;
    data_bind_set_value_pool_enabled(0);
    for (i = 0; i < DATA_BIND_POOL_BENCH_MAX_THREADS; ++i) {
      data_bind_free(g_pool_bench_codecs[i]);
      g_pool_bench_codecs[i] = NULL;
    }
    remove("benchmark_data_bind_pool.tbe");
  }

  bench("Repeated JSON value-tree allocation") {
    data_bind_set_value_pool_enabled(0);
    benchmark("value pool disabled: 82 nodes", DATA_BIND_POOL_BENCH_ITERS,
              DATA_BIND_POOL_BENCH_ITEMS + 2U) {
      DataBindValue *value = NULL;
      if (data_bind_parse_json(g_pool_bench_codecs[0], "Batch", g_pool_bench_json,
                               strlen(g_pool_bench_json), &value, NULL) == DATA_BIND_OK) {
        atomic_fetch_add_explicit(&g_pool_bench_sink, data_bind_value_count(value),
                                  memory_order_relaxed);
      }
      data_bind_value_free(value);
    }

    data_bind_set_value_pool_enabled(1);
    benchmark("value pool enabled: 82 nodes", DATA_BIND_POOL_BENCH_ITERS,
              DATA_BIND_POOL_BENCH_ITEMS + 2U) {
      DataBindValue *value = NULL;
      if (data_bind_parse_json(g_pool_bench_codecs[0], "Batch", g_pool_bench_json,
                               strlen(g_pool_bench_json), &value, NULL) == DATA_BIND_OK) {
        atomic_fetch_add_explicit(&g_pool_bench_sink, data_bind_value_count(value),
                                  memory_order_relaxed);
      }
      data_bind_value_free(value);
    }
  }

  bench("Concurrent JSON value-tree allocation") {
    const size_t nodes_per_parse = DATA_BIND_POOL_BENCH_ITEMS + 2U;

    data_bind_set_value_pool_enabled(0);
    benchmark("disabled: 1 thread", DATA_BIND_POOL_BENCH_PARALLEL_REPEATS,
              DATA_BIND_POOL_BENCH_PARALLEL_ITERS * nodes_per_parse) {
      data_bind_pool_bench_parallel(1U);
    }
    benchmark("disabled: 2 threads", DATA_BIND_POOL_BENCH_PARALLEL_REPEATS,
              2U * DATA_BIND_POOL_BENCH_PARALLEL_ITERS * nodes_per_parse) {
      data_bind_pool_bench_parallel(2U);
    }
    benchmark("disabled: 4 threads", DATA_BIND_POOL_BENCH_PARALLEL_REPEATS,
              4U * DATA_BIND_POOL_BENCH_PARALLEL_ITERS * nodes_per_parse) {
      data_bind_pool_bench_parallel(4U);
    }
    benchmark("disabled: 8 threads", DATA_BIND_POOL_BENCH_PARALLEL_REPEATS,
              8U * DATA_BIND_POOL_BENCH_PARALLEL_ITERS * nodes_per_parse) {
      data_bind_pool_bench_parallel(8U);
    }

    data_bind_set_value_pool_enabled(1);
    benchmark("adaptive atomic: 1 thread", DATA_BIND_POOL_BENCH_PARALLEL_REPEATS,
              DATA_BIND_POOL_BENCH_PARALLEL_ITERS * nodes_per_parse) {
      data_bind_pool_bench_parallel(1U);
    }
    data_bind_set_value_pool_enabled(1);
    benchmark("adaptive atomic: 2 threads", DATA_BIND_POOL_BENCH_PARALLEL_REPEATS,
              2U * DATA_BIND_POOL_BENCH_PARALLEL_ITERS * nodes_per_parse) {
      data_bind_pool_bench_parallel(2U);
    }
    data_bind_set_value_pool_enabled(1);
    benchmark("adaptive atomic: 4 threads", DATA_BIND_POOL_BENCH_PARALLEL_REPEATS,
              4U * DATA_BIND_POOL_BENCH_PARALLEL_ITERS * nodes_per_parse) {
      data_bind_pool_bench_parallel(4U);
    }
    data_bind_set_value_pool_enabled(1);
    benchmark("adaptive atomic: 8 threads", DATA_BIND_POOL_BENCH_PARALLEL_REPEATS,
              8U * DATA_BIND_POOL_BENCH_PARALLEL_ITERS * nodes_per_parse) {
      data_bind_pool_bench_parallel(8U);
    }

    check_size_eq(atomic_load_explicit(&g_pool_bench_failures, memory_order_relaxed), 0U);
  }
}
