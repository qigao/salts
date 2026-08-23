#include "tinytest.h"
#include <cflow/cflow.h>

#include <stdint.h>

#define CFLOW_DIRECT_BENCH_ITEMS 1024u
#define CFLOW_DIRECT_BENCH_SAMPLES 1000u

static volatile uintptr_t cflow_direct_bench_sink;

void cflow_direct_benchmark_consume(const double *data, size_t count);

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

suite("CFlow Direct executor benchmarks") {
  bench("generated Direct and compiled Plan") {
    int input[CFLOW_DIRECT_BENCH_ITEMS];
    double direct_output[CFLOW_DIRECT_BENCH_ITEMS];
    size_t direct_count = 0u;
    size_t index;
    cflow_stream stream = {0};
    cflow_plan plan = {0};
    cflow_result reference = {0};
    cflow_direct_status direct_status = CFLOW_DIRECT_INVALID_ARGUMENT;
    bool plan_ok = false;

    for (index = 0u; index < CFLOW_DIRECT_BENCH_ITEMS; ++index)
      input[index] = (int)index;

    check_true(cflow_direct_bench_pipeline_build(&stream));
    check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
    check_true(cflow_plan_eval_array(&plan, input, CFLOW_DIRECT_BENCH_ITEMS, &reference));
    check_equal(cflow_direct_bench_pipeline_eval_array(input, CFLOW_DIRECT_BENCH_ITEMS,
                                                       direct_output, CFLOW_DIRECT_BENCH_ITEMS,
                                                       &direct_count),
                CFLOW_DIRECT_OK);
    check_equal(reference.count, direct_count);
    check_equal(reference.data, direct_output, direct_count * sizeof(direct_output[0]));
    cflow_result_destroy(&reference);

    benchmark_ops("Direct Filter/Map array", CFLOW_DIRECT_BENCH_SAMPLES, CFLOW_DIRECT_BENCH_ITEMS) {
      direct_status = cflow_direct_bench_pipeline_eval_array(
          input, CFLOW_DIRECT_BENCH_ITEMS, direct_output, CFLOW_DIRECT_BENCH_ITEMS, &direct_count);
      cflow_direct_benchmark_consume(direct_output, direct_count);
      cflow_direct_bench_sink ^= direct_count;
    }
    check_equal(direct_status, CFLOW_DIRECT_OK);
    check_equal(direct_count, (size_t)CFLOW_DIRECT_BENCH_ITEMS / 2u);

    benchmark_ops("Plan Filter/Map array", CFLOW_DIRECT_BENCH_SAMPLES, CFLOW_DIRECT_BENCH_ITEMS) {
      cflow_result result = {0};
      plan_ok = cflow_plan_eval_array(&plan, input, CFLOW_DIRECT_BENCH_ITEMS, &result);
      cflow_direct_benchmark_consume((const double *)result.data, result.count);
      cflow_direct_bench_sink ^= result.count;
      cflow_result_destroy(&result);
    }
    check_true(plan_ok);

    cflow_plan_destroy(&plan);
    cflow_stream_destroy(&stream);
  }
}
