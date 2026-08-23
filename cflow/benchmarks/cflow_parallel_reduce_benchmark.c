#include "tinytest.h"

#include <cflow/cflow.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
  CFLOW_PARALLEL_BENCH_WORKERS = 4u,
  CFLOW_PARALLEL_BENCH_MAX_TASKS = 4u,
  CFLOW_PARALLEL_BENCH_MIN_ITEMS = 256u,
  CFLOW_PARALLEL_BENCH_SMALL_ITEMS = 1024u,
  CFLOW_PARALLEL_BENCH_MEDIUM_ITEMS = 64u * 1024u,
  CFLOW_PARALLEL_BENCH_LARGE_ITEMS = 1024u * 1024u,
  CFLOW_PARALLEL_BENCH_SMALL_SAMPLES = 1000u,
  CFLOW_PARALLEL_BENCH_MEDIUM_SAMPLES = 100u,
  CFLOW_PARALLEL_BENCH_LARGE_SAMPLES = 10u
};

static volatile long cflow_parallel_bench_sink;

typed(reduce, associative, long, cflow_parallel_bench_add,
      (long left, long right)) {
  return left + right;
}

static long cflow_parallel_bench_expected(const long *input, size_t count) {
  long total = 0L;
  for (size_t index = 0u; index < count; ++index) total += input[index];
  return total;
}

#define CFLOW_PARALLEL_BENCH_CASE(label, item_count, sample_count)                 \
  do {                                                                             \
    cflow_result sequential_reference = {0};                                       \
    cflow_result parallel_reference = {0};                                         \
    const long expected = cflow_parallel_bench_expected(input, (item_count));       \
    bool sequential_ok = false;                                                     \
    bool parallel_ok = false;                                                       \
    check_true(cflow_plan_eval_array(                                               \
        &plan, input, (item_count), &sequential_reference));                        \
    check_true(cflow_plan_eval_array_with_options(                                  \
        &plan, input, (item_count), &options, &parallel_reference));                 \
    check_equal(sequential_reference.count, (size_t)1u);                            \
    check_equal(parallel_reference.count, (size_t)1u);                              \
    check_equal(*(const long *)sequential_reference.data, expected);                \
    check_equal(*(const long *)parallel_reference.data, expected);                  \
    check_true(cflow_result_equal(&sequential_reference, &parallel_reference));      \
    cflow_result_destroy(&parallel_reference);                                      \
    cflow_result_destroy(&sequential_reference);                                    \
    benchmark_ops("Plan Reduce sequential / " label, (sample_count), (item_count)) { \
      cflow_result result = {0};                                                     \
      sequential_ok = cflow_plan_eval_array(                                        \
          &plan, input, (item_count), &result);                                     \
      if (sequential_ok && result.count == 1u)                                      \
        cflow_parallel_bench_sink ^= *(const long *)result.data;                    \
      cflow_result_destroy(&result);                                                \
    }                                                                                \
    check_true(sequential_ok);                                                      \
    benchmark_ops("Plan Reduce ordered parallel / " label,                         \
                  (sample_count), (item_count)) {                                   \
      cflow_result result = {0};                                                     \
      parallel_ok = cflow_plan_eval_array_with_options(                             \
          &plan, input, (item_count), &options, &result);                           \
      if (parallel_ok && result.count == 1u)                                        \
        cflow_parallel_bench_sink ^= *(const long *)result.data;                    \
      cflow_result_destroy(&result);                                                \
    }                                                                                \
    check_true(parallel_ok);                                                        \
  } while (0)

suite("CFlow ordered parallel reduce benchmarks") {
  bench("sequential and ordered-parallel Plan reduction") {
    long *input = (long *)malloc(
        (size_t)CFLOW_PARALLEL_BENCH_LARGE_ITEMS * sizeof(*input));
    cflow_stream stream = {0};
    cflow_plan plan = {0};
    cflow_executor executor = {0};
    cflow_plan_eval_options options = {
        .mode = CFLOW_PLAN_EXECUTION_PARALLEL_REDUCE,
        .executor = &executor,
        .max_tasks = CFLOW_PARALLEL_BENCH_MAX_TASKS,
        .min_items_per_task = CFLOW_PARALLEL_BENCH_MIN_ITEMS
    };

    check_not_null(input);
    for (size_t index = 0u; index < CFLOW_PARALLEL_BENCH_LARGE_ITEMS; ++index)
      input[index] = (long)(index % 97u);
    check_not_null(cflow_stream_init(&stream, &cmeta_type_long));
    check_not_null(stream.reduce(&stream, cflow_parallel_bench_add));
    check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
    check_true(cflow_executor_worker_init_with_capacity(
        &executor, CFLOW_PARALLEL_BENCH_WORKERS,
        CFLOW_PARALLEL_BENCH_MAX_TASKS * 2u));
    printf("parallel_reduce_config workers=%u max_tasks=%u min_items_per_task=%u\n",
           CFLOW_PARALLEL_BENCH_WORKERS, CFLOW_PARALLEL_BENCH_MAX_TASKS,
           CFLOW_PARALLEL_BENCH_MIN_ITEMS);

    CFLOW_PARALLEL_BENCH_CASE(
        "1 Ki values", CFLOW_PARALLEL_BENCH_SMALL_ITEMS,
        CFLOW_PARALLEL_BENCH_SMALL_SAMPLES);
    CFLOW_PARALLEL_BENCH_CASE(
        "64 Ki values", CFLOW_PARALLEL_BENCH_MEDIUM_ITEMS,
        CFLOW_PARALLEL_BENCH_MEDIUM_SAMPLES);
    CFLOW_PARALLEL_BENCH_CASE(
        "1 Mi values", CFLOW_PARALLEL_BENCH_LARGE_ITEMS,
        CFLOW_PARALLEL_BENCH_LARGE_SAMPLES);

    cflow_executor_destroy(&executor);
    cflow_plan_destroy(&plan);
    cflow_stream_destroy(&stream);
    free(input);
  }
}

#undef CFLOW_PARALLEL_BENCH_CASE
