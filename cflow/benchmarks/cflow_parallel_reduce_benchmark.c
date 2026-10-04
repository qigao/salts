#include "tinytest.h"

#include <cflow/cflow.h>
#include <cflow/plan_internal.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

typedef struct cflow_parallel_managed_bench_value {
  long value;
} cflow_parallel_managed_bench_value;

static bool cflow_parallel_managed_bench_copy(
    void *destination_, const void *source_) {
  cflow_parallel_managed_bench_value *destination =
      (cflow_parallel_managed_bench_value *)destination_;
  const cflow_parallel_managed_bench_value *source =
      (const cflow_parallel_managed_bench_value *)source_;
  if (!destination || !source) return false;
  destination->value = source->value;
  return true;
}

static void cflow_parallel_managed_bench_move(
    void *destination_, void *source_) {
  cflow_parallel_managed_bench_value *destination =
      (cflow_parallel_managed_bench_value *)destination_;
  cflow_parallel_managed_bench_value *source =
      (cflow_parallel_managed_bench_value *)source_;
  if (!destination || !source) return;
  destination->value = source->value;
  source->value = 0L;
}

static void cflow_parallel_managed_bench_destroy(void *value_) {
  cflow_parallel_managed_bench_value *value =
      (cflow_parallel_managed_bench_value *)value_;
  if (value) value->value = 0L;
}

static const cmeta_type_traits cflow_parallel_managed_bench_traits = {
  .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
  .copy_construct = cflow_parallel_managed_bench_copy,
  .move_construct = cflow_parallel_managed_bench_move,
  .destroy = cflow_parallel_managed_bench_destroy
};

static const cmeta_type_desc cflow_parallel_managed_bench_type = {
  .name = "cflow_parallel_managed_bench_value",
  .size = sizeof(cflow_parallel_managed_bench_value),
  .align = _Alignof(cflow_parallel_managed_bench_value),
  .kind = CMETA_T_OBJECT,
  .pointee = NULL,
  .traits = &cflow_parallel_managed_bench_traits,
  .identity = NULL
};

static bool cflow_parallel_managed_bench_reduce_invoke(
    const cmeta_callable *self, void *out, const void *const *args) {
  const cflow_parallel_managed_bench_value *left;
  const cflow_parallel_managed_bench_value *right;
  (void)self;
  if (!out || !args || !args[0] || !args[1]) return false;
  left = (const cflow_parallel_managed_bench_value *)args[0];
  right = (const cflow_parallel_managed_bench_value *)args[1];
  ((cflow_parallel_managed_bench_value *)out)->value =
      left->value + right->value;
  return true;
}

static bool cflow_parallel_managed_bench_plan(
    cflow_plan *plan, cflow_plan_impl *impl, cflow_plan_inst *instruction) {
  if (!plan || !impl || !instruction) return false;
  memset(plan, 0, sizeof(*plan));
  memset(impl, 0, sizeof(*impl));
  memset(instruction, 0, sizeof(*instruction));
  instruction->opcode = CMETA_PLAN_REDUCE;
  instruction->step = cflow_plan_step_for_opcode(CMETA_PLAN_REDUCE);
  instruction->input_type = &cflow_parallel_managed_bench_type;
  instruction->output_type = &cflow_parallel_managed_bench_type;
  instruction->call.invoke = cflow_parallel_managed_bench_reduce_invoke;
  instruction->call.input_type = &cflow_parallel_managed_bench_type;
  instruction->call.output_type = &cflow_parallel_managed_bench_type;
  impl->code = instruction;
  impl->count = 1u;
  impl->terminal_reduce_index = 0u;
  impl->parallel_reduce_supported = true;
  impl->managed_values = true;
  plan->impl = impl;
  plan->input_type = &cflow_parallel_managed_bench_type;
  plan->output_type = &cflow_parallel_managed_bench_type;
  return true;
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

#define CFLOW_PARALLEL_MANAGED_BENCH_CASE(label, item_count, sample_count)         \
  do {                                                                             \
    cflow_result sequential_reference = {0};                                       \
    cflow_result parallel_reference = {0};                                         \
    const long expected = cflow_parallel_bench_expected(managed_expected, (item_count)); \
    bool sequential_ok = false;                                                     \
    bool parallel_ok = false;                                                       \
    check_true(cflow_plan_eval_array(                                               \
        &managed_plan, managed_input, (item_count), &sequential_reference));        \
    check_true(cflow_plan_eval_array_with_options(                                  \
        &managed_plan, managed_input, (item_count), &options, &parallel_reference)); \
    check_equal(sequential_reference.count, (size_t)1u);                            \
    check_equal(parallel_reference.count, (size_t)1u);                              \
    check_equal(((const cflow_parallel_managed_bench_value *)                       \
                    sequential_reference.data)->value, expected);                   \
    check_equal(((const cflow_parallel_managed_bench_value *)                       \
                    parallel_reference.data)->value, expected);                     \
    cflow_result_destroy(&parallel_reference);                                      \
    cflow_result_destroy(&sequential_reference);                                    \
    benchmark_ops("Managed Plan Reduce sequential / " label,                       \
                  (sample_count), (item_count)) {                                   \
      cflow_result result = {0};                                                     \
      sequential_ok = cflow_plan_eval_array(                                        \
          &managed_plan, managed_input, (item_count), &result);                     \
      if (sequential_ok && result.count == 1u)                                      \
        cflow_parallel_bench_sink ^=                                                \
            ((const cflow_parallel_managed_bench_value *)result.data)->value;       \
      cflow_result_destroy(&result);                                                \
    }                                                                                \
    check_true(sequential_ok);                                                      \
    benchmark_ops("Managed Plan Reduce ordered parallel / " label,                 \
                  (sample_count), (item_count)) {                                   \
      cflow_result result = {0};                                                     \
      parallel_ok = cflow_plan_eval_array_with_options(                             \
          &managed_plan, managed_input, (item_count), &options, &result);           \
      if (parallel_ok && result.count == 1u)                                        \
        cflow_parallel_bench_sink ^=                                                \
            ((const cflow_parallel_managed_bench_value *)result.data)->value;       \
      cflow_result_destroy(&result);                                                \
    }                                                                                \
    check_true(parallel_ok);                                                        \
  } while (0)

suite("CFlow ordered parallel reduce benchmarks") {
  bench("sequential and ordered-parallel Plan reduction") {
    long *input = (long *)malloc(
        (size_t)CFLOW_PARALLEL_BENCH_LARGE_ITEMS * sizeof(*input));
    long *managed_expected = (long *)malloc(
        (size_t)CFLOW_PARALLEL_BENCH_LARGE_ITEMS * sizeof(*managed_expected));
    cflow_parallel_managed_bench_value *managed_input =
        (cflow_parallel_managed_bench_value *)malloc(
            (size_t)CFLOW_PARALLEL_BENCH_LARGE_ITEMS * sizeof(*managed_input));
    cflow_stream stream = {0};
    cflow_plan plan = {0};
    cflow_plan managed_plan = {0};
    cflow_plan_impl managed_impl = {0};
    cflow_plan_inst managed_instruction = {0};
    cflow_executor executor = {0};
    cflow_plan_eval_options options = {
        .mode = CFLOW_PLAN_EXECUTION_PARALLEL_REDUCE,
        .executor = &executor,
        .max_tasks = CFLOW_PARALLEL_BENCH_MAX_TASKS,
        .min_items_per_task = CFLOW_PARALLEL_BENCH_MIN_ITEMS
    };

    check_not_null(input);
    check_not_null(managed_expected);
    check_not_null(managed_input);
    for (size_t index = 0u; index < CFLOW_PARALLEL_BENCH_LARGE_ITEMS; ++index) {
      const long value = (long)(index % 97u);
      input[index] = value;
      managed_expected[index] = value;
      managed_input[index].value = value;
    }
    check_not_null(cflow_stream_init(&stream, &cmeta_type_long));
    check_not_null(stream.reduce(&stream, cflow_parallel_bench_add));
    check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
    check_true(cflow_parallel_managed_bench_plan(
        &managed_plan, &managed_impl, &managed_instruction));
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

    printf("managed_parallel_reduce lifecycle=copy/move/destroy heap_payload=false\n");
    CFLOW_PARALLEL_MANAGED_BENCH_CASE(
        "1 Ki values", CFLOW_PARALLEL_BENCH_SMALL_ITEMS,
        CFLOW_PARALLEL_BENCH_SMALL_SAMPLES / 2u);
    CFLOW_PARALLEL_MANAGED_BENCH_CASE(
        "64 Ki values", CFLOW_PARALLEL_BENCH_MEDIUM_ITEMS,
        CFLOW_PARALLEL_BENCH_MEDIUM_SAMPLES / 2u);
    CFLOW_PARALLEL_MANAGED_BENCH_CASE(
        "1 Mi values", CFLOW_PARALLEL_BENCH_LARGE_ITEMS,
        CFLOW_PARALLEL_BENCH_LARGE_SAMPLES / 2u);

    cflow_executor_destroy(&executor);
    cflow_plan_destroy(&plan);
    cflow_stream_destroy(&stream);
    for (size_t index = 0u; index < CFLOW_PARALLEL_BENCH_LARGE_ITEMS; ++index)
      cflow_parallel_managed_bench_destroy(&managed_input[index]);
    free(managed_input);
    free(managed_expected);
    free(input);
  }
}

#undef CFLOW_PARALLEL_MANAGED_BENCH_CASE
#undef CFLOW_PARALLEL_BENCH_CASE
