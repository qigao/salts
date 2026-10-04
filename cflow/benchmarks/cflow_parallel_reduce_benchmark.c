#include "tinytest.h"

#include <cflow/cflow.h>
#include <cflow/plan_internal.h>

#include <stdint.h>
#include <stdatomic.h>
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

typedef struct cflow_parallel_managed_probe_counts {
  atomic_size_t copies;
  atomic_size_t moves;
  atomic_size_t destroys;
  atomic_size_t invokes;
  atomic_size_t active_invokes;
  atomic_size_t peak_invokes;
} cflow_parallel_managed_probe_counts;

typedef struct cflow_parallel_managed_probe_snapshot {
  size_t copies;
  size_t moves;
  size_t destroys;
  size_t invokes;
  size_t active_invokes;
  size_t peak_invokes;
} cflow_parallel_managed_probe_snapshot;

static cflow_parallel_managed_probe_counts cflow_parallel_managed_probe;

static void cflow_parallel_managed_probe_reset(void) {
  atomic_store_explicit(
      &cflow_parallel_managed_probe.copies, 0u, memory_order_relaxed);
  atomic_store_explicit(
      &cflow_parallel_managed_probe.moves, 0u, memory_order_relaxed);
  atomic_store_explicit(
      &cflow_parallel_managed_probe.destroys, 0u, memory_order_relaxed);
  atomic_store_explicit(
      &cflow_parallel_managed_probe.invokes, 0u, memory_order_relaxed);
  atomic_store_explicit(
      &cflow_parallel_managed_probe.active_invokes, 0u, memory_order_relaxed);
  atomic_store_explicit(
      &cflow_parallel_managed_probe.peak_invokes, 0u, memory_order_relaxed);
}

static cflow_parallel_managed_probe_snapshot
cflow_parallel_managed_probe_read(void) {
  cflow_parallel_managed_probe_snapshot snapshot = {
      atomic_load_explicit(
          &cflow_parallel_managed_probe.copies, memory_order_relaxed),
      atomic_load_explicit(
          &cflow_parallel_managed_probe.moves, memory_order_relaxed),
      atomic_load_explicit(
          &cflow_parallel_managed_probe.destroys, memory_order_relaxed),
      atomic_load_explicit(
          &cflow_parallel_managed_probe.invokes, memory_order_relaxed),
      atomic_load_explicit(
          &cflow_parallel_managed_probe.active_invokes, memory_order_relaxed),
      atomic_load_explicit(
          &cflow_parallel_managed_probe.peak_invokes, memory_order_relaxed)
  };
  return snapshot;
}

static bool cflow_parallel_managed_probe_copy(
    void *destination, const void *source) {
  atomic_fetch_add_explicit(
      &cflow_parallel_managed_probe.copies, 1u, memory_order_relaxed);
  return cflow_parallel_managed_bench_copy(destination, source);
}

static void cflow_parallel_managed_probe_move(
    void *destination, void *source) {
  atomic_fetch_add_explicit(
      &cflow_parallel_managed_probe.moves, 1u, memory_order_relaxed);
  cflow_parallel_managed_bench_move(destination, source);
}

static void cflow_parallel_managed_probe_destroy(void *value) {
  atomic_fetch_add_explicit(
      &cflow_parallel_managed_probe.destroys, 1u, memory_order_relaxed);
  cflow_parallel_managed_bench_destroy(value);
}

static const cmeta_type_traits cflow_parallel_managed_probe_traits = {
  .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
  .copy_construct = cflow_parallel_managed_probe_copy,
  .move_construct = cflow_parallel_managed_probe_move,
  .destroy = cflow_parallel_managed_probe_destroy
};

static const cmeta_type_desc cflow_parallel_managed_probe_type = {
  .name = "cflow_parallel_managed_probe_value",
  .size = sizeof(cflow_parallel_managed_bench_value),
  .align = _Alignof(cflow_parallel_managed_bench_value),
  .kind = CMETA_T_OBJECT,
  .pointee = NULL,
  .traits = &cflow_parallel_managed_probe_traits,
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

static bool cflow_parallel_managed_probe_reduce_invoke(
    const cmeta_callable *self, void *out, const void *const *args) {
  const size_t active =
      atomic_fetch_add_explicit(
          &cflow_parallel_managed_probe.active_invokes,
          1u, memory_order_relaxed) + 1u;
  size_t peak = atomic_load_explicit(
      &cflow_parallel_managed_probe.peak_invokes, memory_order_relaxed);
  bool ok;

  while (peak < active &&
         !atomic_compare_exchange_weak_explicit(
             &cflow_parallel_managed_probe.peak_invokes,
             &peak, active,
             memory_order_relaxed, memory_order_relaxed)) {
  }
  atomic_fetch_add_explicit(
      &cflow_parallel_managed_probe.invokes, 1u, memory_order_relaxed);
  ok = cflow_parallel_managed_bench_reduce_invoke(self, out, args);
  atomic_fetch_sub_explicit(
      &cflow_parallel_managed_probe.active_invokes,
      1u, memory_order_relaxed);
  return ok;
}

static bool cflow_parallel_managed_bench_plan_for(
    cflow_plan *plan, cflow_plan_impl *impl, cflow_plan_inst *instruction,
    const cmeta_type_desc *type,
    bool (*invoke)(const cmeta_callable *, void *, const void *const *)) {
  if (!plan || !impl || !instruction || !type || !invoke) return false;
  memset(plan, 0, sizeof(*plan));
  memset(impl, 0, sizeof(*impl));
  memset(instruction, 0, sizeof(*instruction));
  instruction->opcode = CMETA_PLAN_REDUCE;
  instruction->step = cflow_plan_step_for_opcode(CMETA_PLAN_REDUCE);
  instruction->input_type = type;
  instruction->output_type = type;
  instruction->call.invoke = invoke;
  instruction->call.input_type = type;
  instruction->call.output_type = type;
  impl->code = instruction;
  impl->count = 1u;
  impl->terminal_reduce_index = 0u;
  impl->parallel_reduce_supported = true;
  impl->managed_values = true;
  plan->impl = impl;
  plan->input_type = type;
  plan->output_type = type;
  return true;
}

static bool cflow_parallel_managed_bench_plan(
    cflow_plan *plan, cflow_plan_impl *impl, cflow_plan_inst *instruction) {
  return cflow_parallel_managed_bench_plan_for(
      plan, impl, instruction, &cflow_parallel_managed_bench_type,
      cflow_parallel_managed_bench_reduce_invoke);
}

static bool cflow_parallel_managed_probe_plan(
    cflow_plan *plan, cflow_plan_impl *impl, cflow_plan_inst *instruction) {
  return cflow_parallel_managed_bench_plan_for(
      plan, impl, instruction, &cflow_parallel_managed_probe_type,
      cflow_parallel_managed_probe_reduce_invoke);
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


#define CFLOW_PARALLEL_MANAGED_WORKER_BENCH_CASE(label, worker_count, task_count) \
  do {                                                                             \
    cflow_executor scaling_executor = {0};                                         \
    cflow_executor_control scaling_control = {0};                                  \
    cflow_executor_protocol_stats scaling_stats = {0};                             \
    const size_t scaling_tasks = (task_count);                                     \
    cflow_plan_eval_options scaling_options = {                                    \
        .mode = CFLOW_PLAN_EXECUTION_PARALLEL_REDUCE,                              \
        .executor = &scaling_executor,                                              \
        .max_tasks = scaling_tasks,                                                 \
        .min_items_per_task = CFLOW_PARALLEL_BENCH_MIN_ITEMS                       \
    };                                                                             \
    bool scaling_ok = false;                                                       \
    check_true(cflow_executor_worker_init_with_capacity(                            \
        &scaling_executor, (worker_count), scaling_tasks * 2u));                   \
    check_true(cflow_executor_as_control(&scaling_executor, &scaling_control));     \
    benchmark_ops("Managed Plan Reduce ordered parallel / 1 Mi values / " label,   \
                  CFLOW_PARALLEL_BENCH_LARGE_SAMPLES / 2u,                         \
                  CFLOW_PARALLEL_BENCH_LARGE_ITEMS) {                              \
      cflow_result result = {0};                                                    \
      scaling_ok = cflow_plan_eval_array_with_options(                             \
          &managed_plan, managed_input, CFLOW_PARALLEL_BENCH_LARGE_ITEMS,          \
          &scaling_options, &result);                                               \
      if (scaling_ok && result.count == 1u)                                        \
        cflow_parallel_bench_sink ^=                                               \
            ((const cflow_parallel_managed_bench_value *)result.data)->value;      \
      cflow_result_destroy(&result);                                                \
    }                                                                              \
    check_true(scaling_ok);                                                        \
    check_true(cflow_executor_control_get_stats(                                   \
        &scaling_control, &scaling_stats));                                        \
    check_equal(scaling_stats.accepted, scaling_stats.completed);                  \
    check_equal(scaling_stats.cancelled, (size_t)0u);                              \
    printf("managed_parallel_reduce_executor workers=%zu tasks=%zu "               \
           "accepted=%zu completed=%zu cancelled=%zu rejected_full=%zu "           \
           "rejected_closed=%zu rejected_would_block=%zu\\n",                    \
           (size_t)(worker_count), scaling_tasks,                                  \
           scaling_stats.accepted, scaling_stats.completed,                        \
           scaling_stats.cancelled, scaling_stats.rejected_full,                   \
           scaling_stats.rejected_closed, scaling_stats.rejected_would_block);     \
    {                                                                              \
      cflow_result concurrency_probe = {0};                                        \
      cflow_parallel_managed_probe_snapshot concurrency_counts;                    \
      cflow_parallel_managed_probe_reset();                                        \
      check_true(cflow_plan_eval_array_with_options(                               \
          &managed_probe_plan, managed_input, CFLOW_PARALLEL_BENCH_MEDIUM_ITEMS,   \
          &scaling_options, &concurrency_probe));                                  \
      cflow_result_destroy(&concurrency_probe);                                    \
      concurrency_counts = cflow_parallel_managed_probe_read();                    \
      check_equal(concurrency_counts.active_invokes, (size_t)0u);                  \
      check(concurrency_counts.peak_invokes >= 1u);                                \
      check(concurrency_counts.peak_invokes <= (size_t)(worker_count));            \
      check(concurrency_counts.peak_invokes <= scaling_tasks);                     \
      printf("managed_parallel_reduce_concurrency workers=%zu tasks=%zu "           \
             "items=%u peak_invokes=%zu invokes=%zu\\n",                         \
             (size_t)(worker_count), scaling_tasks,                                \
             CFLOW_PARALLEL_BENCH_MEDIUM_ITEMS,                                    \
             concurrency_counts.peak_invokes, concurrency_counts.invokes);         \
    }                                                                              \
    cflow_executor_destroy(&scaling_executor);                                     \
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
    cflow_plan managed_probe_plan = {0};
    cflow_plan_impl managed_probe_impl = {0};
    cflow_plan_inst managed_probe_instruction = {0};
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
    check_true(cflow_parallel_managed_probe_plan(
        &managed_probe_plan, &managed_probe_impl, &managed_probe_instruction));
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

    {
      cflow_result sequential_probe = {0};
      cflow_result parallel_probe = {0};
      cflow_parallel_managed_probe_snapshot sequential_counts;
      cflow_parallel_managed_probe_snapshot parallel_counts;

      cflow_parallel_managed_probe_reset();
      check_true(cflow_plan_eval_array(
          &managed_probe_plan, managed_input, CFLOW_PARALLEL_BENCH_SMALL_ITEMS,
          &sequential_probe));
      cflow_result_destroy(&sequential_probe);
      sequential_counts = cflow_parallel_managed_probe_read();
      check_equal(
          sequential_counts.invokes,
          (size_t)CFLOW_PARALLEL_BENCH_SMALL_ITEMS - 1u);

      cflow_parallel_managed_probe_reset();
      check_true(cflow_plan_eval_array_with_options(
          &managed_probe_plan, managed_input, CFLOW_PARALLEL_BENCH_SMALL_ITEMS,
          &options, &parallel_probe));
      cflow_result_destroy(&parallel_probe);
      parallel_counts = cflow_parallel_managed_probe_read();
      check_equal(
          parallel_counts.invokes,
          (size_t)CFLOW_PARALLEL_BENCH_SMALL_ITEMS - 1u);

      printf(
          "managed_lifecycle_counts mode=sequential items=%u copy=%zu move=%zu "
          "destroy=%zu invoke=%zu\n",
          CFLOW_PARALLEL_BENCH_SMALL_ITEMS,
          sequential_counts.copies, sequential_counts.moves,
          sequential_counts.destroys, sequential_counts.invokes);
      printf(
          "managed_lifecycle_counts mode=parallel workers=%u items=%u copy=%zu "
          "move=%zu destroy=%zu invoke=%zu\n",
          CFLOW_PARALLEL_BENCH_WORKERS, CFLOW_PARALLEL_BENCH_SMALL_ITEMS,
          parallel_counts.copies, parallel_counts.moves,
          parallel_counts.destroys, parallel_counts.invokes);
    }

    CFLOW_PARALLEL_MANAGED_BENCH_CASE(
        "1 Ki values", CFLOW_PARALLEL_BENCH_SMALL_ITEMS,
        CFLOW_PARALLEL_BENCH_SMALL_SAMPLES / 2u);
    CFLOW_PARALLEL_MANAGED_BENCH_CASE(
        "64 Ki values", CFLOW_PARALLEL_BENCH_MEDIUM_ITEMS,
        CFLOW_PARALLEL_BENCH_MEDIUM_SAMPLES / 2u);
    CFLOW_PARALLEL_MANAGED_BENCH_CASE(
        "1 Mi values", CFLOW_PARALLEL_BENCH_LARGE_ITEMS,
        CFLOW_PARALLEL_BENCH_LARGE_SAMPLES / 2u);

    printf(
        "managed_parallel_reduce_worker_scaling items=%u "
        "workers/tasks are varied independently\n",
        CFLOW_PARALLEL_BENCH_LARGE_ITEMS);
    CFLOW_PARALLEL_MANAGED_WORKER_BENCH_CASE("workers=1 tasks=2", 1u, 2u);
    CFLOW_PARALLEL_MANAGED_WORKER_BENCH_CASE("workers=2 tasks=2", 2u, 2u);
    CFLOW_PARALLEL_MANAGED_WORKER_BENCH_CASE("workers=2 tasks=4", 2u, 4u);
    CFLOW_PARALLEL_MANAGED_WORKER_BENCH_CASE("workers=4 tasks=2", 4u, 2u);
    CFLOW_PARALLEL_MANAGED_WORKER_BENCH_CASE("workers=4 tasks=4", 4u, 4u);

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

#undef CFLOW_PARALLEL_MANAGED_WORKER_BENCH_CASE
#undef CFLOW_PARALLEL_MANAGED_BENCH_CASE
#undef CFLOW_PARALLEL_BENCH_CASE
