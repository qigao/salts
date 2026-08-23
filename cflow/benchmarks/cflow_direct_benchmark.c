#include "tinytest.h"
#include <cflow/cflow.h>
#include <cflow/plan_internal.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CFLOW_DIRECT_BENCH_ITEMS 1024u
#define CFLOW_DIRECT_BENCH_SAMPLES 50000u

static volatile uintptr_t cflow_direct_bench_sink;

void cflow_direct_benchmark_consume(const double *data, size_t count);

static bool cflow_direct_bench_fused_typed(const int *input, size_t input_count, double *output,
                                           size_t output_capacity, size_t *output_count);
static bool cflow_direct_bench_fused_typed_owned(const int *input, size_t input_count,
                                                 double **output, size_t *output_count);
static bool cflow_direct_bench_staged_typed_owned(const int *input, size_t input_count,
                                                  double **output, size_t *output_count);
static bool cflow_direct_bench_fused_erased(const int *input, size_t input_count, double *output,
                                            size_t output_capacity, size_t *output_count,
                                            const cmeta_callable *even,
                                            const cmeta_callable *square,
                                            const cmeta_callable *half);

typed(filter, value, bool, cflow_direct_bench_even, (int value)) { return value % 2 == 0; }

typed(map, value, long, cflow_direct_bench_square, (int value)) {
  return (long)value * (long)value;
}

typed(map, value, double, cflow_direct_bench_half, (long value)) { return (double)value / 2.0; }

#define CFlowDirectBenchSteps(M)                                                                   \
  CFlowDirectSteps(M, (filter, int, int, cflow_direct_bench_even),                                 \
                   (map, int, long, cflow_direct_bench_square),                                    \
                   (map, long, double, cflow_direct_bench_half))

cflow_direct_pipeline(cflow_direct_bench_pipeline, int, &cmeta_type_int, double, 3,
                      CFlowDirectBenchSteps);

static bool cflow_direct_bench_fused_typed(const int *input, size_t input_count, double *output,
                                           size_t output_capacity, size_t *output_count) {
  size_t count = 0u;
  size_t index;

  if (!output_count || (!input && input_count != 0u) || (!output && output_capacity != 0u) ||
      output_capacity < input_count)
    return false;
  *output_count = 0u;
  for (index = 0u; index < input_count; ++index) {
    long squared;
    if (!typed_call(cflow_direct_bench_even)(input[index])) continue;
    squared = typed_call(cflow_direct_bench_square)(input[index]);
    output[count++] = typed_call(cflow_direct_bench_half)(squared);
  }
  *output_count = count;
  return true;
}

static bool cflow_direct_bench_fused_typed_owned(const int *input, size_t input_count,
                                                 double **output, size_t *output_count) {
  double *values;
  size_t count = 0u;
  size_t index;

  if (!output || !output_count || (!input && input_count != 0u) ||
      input_count > SIZE_MAX / sizeof(*values))
    return false;
  *output = NULL;
  *output_count = 0u;
  values = input_count ? (double *)malloc(input_count * sizeof(*values)) : NULL;
  if (input_count && !values) return false;

  for (index = 0u; index < input_count; ++index) {
    long squared;
    if (!typed_call(cflow_direct_bench_even)(input[index])) continue;
    squared = typed_call(cflow_direct_bench_square)(input[index]);
    values[count++] = typed_call(cflow_direct_bench_half)(squared);
  }

  *output = values;
  *output_count = count;
  return true;
}

static bool cflow_direct_bench_staged_typed_owned(const int *input, size_t input_count,
                                                  double **output, size_t *output_count) {
  int *filtered = NULL;
  long *squared = NULL;
  double *halved = NULL;
  size_t filtered_count = 0u;
  size_t index;

  if (!output || !output_count || (!input && input_count != 0u) ||
      input_count > SIZE_MAX / sizeof(*filtered))
    return false;
  *output = NULL;
  *output_count = 0u;
  filtered = input_count ? (int *)malloc(input_count * sizeof(*filtered)) : NULL;
  if (input_count && !filtered) return false;
  if (input_count) memcpy(filtered, input, input_count * sizeof(*filtered));

  for (index = 0u; index < input_count; ++index) {
    if (!typed_call(cflow_direct_bench_even)(filtered[index])) continue;
    filtered[filtered_count++] = filtered[index];
  }

  if (filtered_count > SIZE_MAX / sizeof(*squared)) goto fail;
  squared = filtered_count ? (long *)malloc(filtered_count * sizeof(*squared)) : NULL;
  if (filtered_count && !squared) goto fail;
  for (index = 0u; index < filtered_count; ++index)
    squared[index] = typed_call(cflow_direct_bench_square)(filtered[index]);
  free(filtered);
  filtered = NULL;

  if (filtered_count > SIZE_MAX / sizeof(*halved)) goto fail;
  halved = filtered_count ? (double *)malloc(filtered_count * sizeof(*halved)) : NULL;
  if (filtered_count && !halved) goto fail;
  for (index = 0u; index < filtered_count; ++index)
    halved[index] = typed_call(cflow_direct_bench_half)(squared[index]);
  free(squared);

  *output = halved;
  *output_count = filtered_count;
  return true;

fail:
  free(filtered);
  free(squared);
  return false;
}

static bool cflow_direct_bench_fused_erased(const int *input, size_t input_count, double *output,
                                            size_t output_capacity, size_t *output_count,
                                            const cmeta_callable *even,
                                            const cmeta_callable *square,
                                            const cmeta_callable *half) {
  size_t count = 0u;
  size_t index;

  if (!output_count || !even || !square || !half || (!input && input_count != 0u) ||
      (!output && output_capacity != 0u) || output_capacity < input_count)
    return false;
  *output_count = 0u;
  for (index = 0u; index < input_count; ++index) {
    bool keep = false;
    long squared;
    double halved;
    const void *filter_args[1] = {&input[index]};
    const void *square_args[1] = {&input[index]};
    const void *half_args[1] = {&squared};

    if (!cmeta_callable_invoke(even, &keep, filter_args)) return false;
    if (!keep) continue;
    if (!cmeta_callable_invoke(square, &squared, square_args) ||
        !cmeta_callable_invoke(half, &halved, half_args))
      return false;
    output[count++] = halved;
  }
  *output_count = count;
  return true;
}

suite("CFlow Direct executor benchmarks") {
  bench("generated Direct and compiled Plan (input-item throughput)") {
    int input[CFLOW_DIRECT_BENCH_ITEMS];
    double direct_output[CFLOW_DIRECT_BENCH_ITEMS];
    size_t direct_count = 0u;
    size_t index;
    cflow_stream stream = {0};
    cflow_plan plan = {0};
    cflow_plan materialized_plan = {0};
    cflow_plan_compile_stats plan_stats = {0};
    cflow_result reference = {0};
    cflow_result profiled = {0};
    cflow_result materialized_reference = {0};
    cflow_plan_eval_stats eval_stats = {0};
    cmeta_callable bound_even = {0};
    cmeta_callable bound_square = {0};
    cmeta_callable bound_half = {0};
    double *fused_owned_output = NULL;
    double *staged_owned_output = NULL;
    double fused_output[CFLOW_DIRECT_BENCH_ITEMS];
    double erased_output[CFLOW_DIRECT_BENCH_ITEMS];
    size_t fused_count = 0u;
    size_t fused_owned_count = 0u;
    size_t staged_owned_count = 0u;
    size_t erased_count = 0u;
    cflow_direct_status direct_status = CFLOW_DIRECT_INVALID_ARGUMENT;
    bool plan_ok = false;
    bool materialized_plan_ok = false;
    bool fused_ok = false;
    bool fused_owned_ok = false;
    bool staged_owned_ok = false;
    bool erased_ok = false;

    for (index = 0u; index < CFLOW_DIRECT_BENCH_ITEMS; ++index)
      input[index] = (int)index;

    check_true(cflow_direct_bench_pipeline_build(&stream));
    check_true(cflow_plan_compile_surface(&plan, &stream.graph, &plan_stats));
    check_true(cflow_plan_compile_surface(&materialized_plan, &stream.graph, NULL));
    /* Use identical compiled instructions as the paired control; only the new
       eligibility decision differs. */
    ((cflow_plan_impl *)materialized_plan.impl)->fused_value = false;
    check_equal(plan_stats.instructions, (size_t)2u);
    check_equal(plan_stats.map_callbacks, (size_t)2u);
    check_true(cmeta_callable_bind(cflow_direct_bench_even.fn, &bound_even));
    check_true(cmeta_callable_bind(cflow_direct_bench_square.fn, &bound_square));
    check_true(cmeta_callable_bind(cflow_direct_bench_half.fn, &bound_half));
    check_true(cflow_plan_eval_array(&plan, input, CFLOW_DIRECT_BENCH_ITEMS, &reference));
    check_true(cflow_plan_eval_array(&materialized_plan, input, CFLOW_DIRECT_BENCH_ITEMS,
                                     &materialized_reference));
    check_true(cflow_result_equal(&reference, &materialized_reference));
    cflow_result_destroy(&materialized_reference);
    check_true(cflow_plan_eval_array_profile(&plan, input, CFLOW_DIRECT_BENCH_ITEMS,
                                             &profiled, &eval_stats));
    check_true(cflow_result_equal(&reference, &profiled));
    check_true(eval_stats.fused_value_path);
    check_equal(eval_stats.allocation_calls, (size_t)3u);
    check_equal(eval_stats.selection_bytes, (size_t)128u);
    check_equal(eval_stats.intermediate_bytes,
                (size_t)CFLOW_DIRECT_BENCH_ITEMS / 2u * sizeof(long));
    check_equal(eval_stats.result_bytes,
                (size_t)CFLOW_DIRECT_BENCH_ITEMS / 2u * sizeof(double));
    check_equal(eval_stats.allocated_bytes,
                (size_t)128u +
                    (size_t)CFLOW_DIRECT_BENCH_ITEMS / 2u * sizeof(long) +
                    (size_t)CFLOW_DIRECT_BENCH_ITEMS / 2u * sizeof(double));
    check_equal(eval_stats.peak_live_bytes,
                (size_t)CFLOW_DIRECT_BENCH_ITEMS / 2u *
                    (sizeof(long) + sizeof(double)));
    check_equal(eval_stats.staged_input_copy_bytes, (size_t)0u);
    cflow_result_destroy(&profiled);
    check_equal(cflow_direct_bench_pipeline_eval_array(input, CFLOW_DIRECT_BENCH_ITEMS,
                                                       direct_output, CFLOW_DIRECT_BENCH_ITEMS,
                                                       &direct_count),
                CFLOW_DIRECT_OK);
    check_equal(reference.count, direct_count);
    check_equal(reference.data, direct_output, direct_count * sizeof(direct_output[0]));
    check_true(cflow_direct_bench_fused_typed(input, CFLOW_DIRECT_BENCH_ITEMS, fused_output,
                                              CFLOW_DIRECT_BENCH_ITEMS, &fused_count));
    check_equal(reference.count, fused_count);
    check_equal(reference.data, fused_output, fused_count * sizeof(fused_output[0]));
    check_true(cflow_direct_bench_fused_typed_owned(input, CFLOW_DIRECT_BENCH_ITEMS,
                                                    &fused_owned_output, &fused_owned_count));
    check_equal(reference.count, fused_owned_count);
    check_equal(reference.data, fused_owned_output,
                fused_owned_count * sizeof(fused_owned_output[0]));
    check_true(cflow_direct_bench_staged_typed_owned(input, CFLOW_DIRECT_BENCH_ITEMS,
                                                     &staged_owned_output, &staged_owned_count));
    check_equal(reference.count, staged_owned_count);
    check_equal(reference.data, staged_owned_output,
                staged_owned_count * sizeof(staged_owned_output[0]));
    check_true(cflow_direct_bench_fused_erased(input, CFLOW_DIRECT_BENCH_ITEMS, erased_output,
                                               CFLOW_DIRECT_BENCH_ITEMS, &erased_count, &bound_even,
                                               &bound_square, &bound_half));
    check_equal(reference.count, erased_count);
    check_equal(reference.data, erased_output, erased_count * sizeof(erased_output[0]));
    free(fused_owned_output);
    free(staged_owned_output);
    cflow_result_destroy(&reference);

    benchmark_ops("Direct Filter/Map array", CFLOW_DIRECT_BENCH_SAMPLES, CFLOW_DIRECT_BENCH_ITEMS) {
      direct_status = cflow_direct_bench_pipeline_eval_array(
          input, CFLOW_DIRECT_BENCH_ITEMS, direct_output, CFLOW_DIRECT_BENCH_ITEMS, &direct_count);
      cflow_direct_benchmark_consume(direct_output, direct_count);
      cflow_direct_bench_sink ^= direct_count;
    }
    check_equal(direct_status, CFLOW_DIRECT_OK);
    check_equal(direct_count, (size_t)CFLOW_DIRECT_BENCH_ITEMS / 2u);

    benchmark_ops("Typed fused caller-buffer Filter/Map", CFLOW_DIRECT_BENCH_SAMPLES,
                  CFLOW_DIRECT_BENCH_ITEMS) {
      fused_ok = cflow_direct_bench_fused_typed(input, CFLOW_DIRECT_BENCH_ITEMS, fused_output,
                                                CFLOW_DIRECT_BENCH_ITEMS, &fused_count);
      cflow_direct_benchmark_consume(fused_output, fused_count);
      cflow_direct_bench_sink ^= fused_count;
    }
    check_true(fused_ok);

    benchmark_ops("Typed fused owned Filter/Map", CFLOW_DIRECT_BENCH_SAMPLES,
                  CFLOW_DIRECT_BENCH_ITEMS) {
      double *result = NULL;
      size_t result_count = 0u;
      fused_owned_ok = cflow_direct_bench_fused_typed_owned(input, CFLOW_DIRECT_BENCH_ITEMS,
                                                            &result, &result_count);
      cflow_direct_benchmark_consume(result, result_count);
      cflow_direct_bench_sink ^= result_count;
      free(result);
    }
    check_true(fused_owned_ok);

    benchmark_ops("Typed staged owned Filter/Map", CFLOW_DIRECT_BENCH_SAMPLES,
                  CFLOW_DIRECT_BENCH_ITEMS) {
      double *result = NULL;
      size_t result_count = 0u;
      staged_owned_ok = cflow_direct_bench_staged_typed_owned(input, CFLOW_DIRECT_BENCH_ITEMS,
                                                              &result, &result_count);
      cflow_direct_benchmark_consume(result, result_count);
      cflow_direct_bench_sink ^= result_count;
      free(result);
    }
    check_true(staged_owned_ok);

    benchmark_ops("Erased fused caller-buffer Filter/Map", CFLOW_DIRECT_BENCH_SAMPLES,
                  CFLOW_DIRECT_BENCH_ITEMS) {
      erased_ok = cflow_direct_bench_fused_erased(input, CFLOW_DIRECT_BENCH_ITEMS, erased_output,
                                                  CFLOW_DIRECT_BENCH_ITEMS, &erased_count,
                                                  &bound_even, &bound_square, &bound_half);
      cflow_direct_benchmark_consume(erased_output, erased_count);
      cflow_direct_bench_sink ^= erased_count;
    }
    check_true(erased_ok);

    benchmark_ops("Plan materialized baseline", CFLOW_DIRECT_BENCH_SAMPLES,
                  CFLOW_DIRECT_BENCH_ITEMS) {
      cflow_result result = {0};
      materialized_plan_ok = cflow_plan_eval_array(&materialized_plan, input,
                                                   CFLOW_DIRECT_BENCH_ITEMS, &result);
      cflow_direct_benchmark_consume((const double *)result.data, result.count);
      cflow_direct_bench_sink ^= result.count;
      cflow_result_destroy(&result);
    }
    check_true(materialized_plan_ok);

    benchmark_ops("Plan Filter/Map array", CFLOW_DIRECT_BENCH_SAMPLES, CFLOW_DIRECT_BENCH_ITEMS) {
      cflow_result result = {0};
      plan_ok = cflow_plan_eval_array(&plan, input, CFLOW_DIRECT_BENCH_ITEMS, &result);
      cflow_direct_benchmark_consume((const double *)result.data, result.count);
      cflow_direct_bench_sink ^= result.count;
      cflow_result_destroy(&result);
    }
    check_true(plan_ok);

    cflow_plan_destroy(&materialized_plan);
    cflow_plan_destroy(&plan);
    cflow_stream_destroy(&stream);
  }
}
