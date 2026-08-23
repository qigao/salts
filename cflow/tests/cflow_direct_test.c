#include "tinytest.h"
#include <cflow/cflow.h>
#include <cflow/plan_internal.h>

#include <string.h>

typedef struct cflow_direct_fixture {
  cflow_stream stream;
  cflow_plan plan;
  cflow_result plan_result;
  cflow_result kernel_result;
} cflow_direct_fixture;

static cflow_direct_fixture cflow_direct_state;

typed(filter, value, bool, cflow_direct_even, (int value)) { return value % 2 == 0; }

typed(map, value, long, cflow_direct_square, (int value)) { return (long)value * (long)value; }

typed(map, value, double, cflow_direct_half, (long value)) { return (double)value / 2.0; }

typed(map, stateful, long, cflow_direct_stateful_square, (int value)) {
  return (long)value * (long)value;
}

typed(map, value, int, cflow_direct_plus_one, (int value)) { return value + 1; }

typed(map, value, int, cflow_direct_times_two, (int value)) { return value * 2; }

static size_t cflow_direct_erased_invocations;

static long cmeta_typed_cflow_direct_trap_map(int value) { return (long)value * 3L; }

static cmeta_fn cflow_direct_trap_meta(void) {
  cmeta_fn meta = CFLOW_WRAP_OP_TYPED(map, cmeta_typed_cflow_direct_trap_map);
  meta.effects = CMETA_CONTRACT_EFFECTS(value);
  meta.properties = CMETA_CONTRACT_PROPERTIES(value);
  return meta;
}

static bool cflow_direct_trap_invoke(const cmeta_callable *self, void *out,
                                     const void *const *args) {
  if (self == NULL) return false;
  ++cflow_direct_erased_invocations;
  return cmeta_fn_invoke(self->meta, out, args);
}

static cmeta_gen_status cflow_direct_trap_generate(const cmeta_callable *self, const void *input,
                                                   void *out, size_t *cursor) {
  (void)self;
  (void)input;
  (void)out;
  (void)cursor;
  return CMETA_GEN_ERROR;
}

const cflow_map_callable cflow_direct_trap_map = {
    .fn = CMETA_CALLABLE_INIT(CMETA_CONTRACT_EFFECTS(value), CMETA_CONTRACT_PROPERTIES(value),
                              cflow_direct_trap_meta, cflow_direct_trap_invoke,
                              cflow_direct_trap_generate, 0u)};

#define CFlowDirectTestSteps(M)                                                                    \
  CFlowDirectSteps(M, (filter, int, int, cflow_direct_even),                                       \
                   (map, int, long, cflow_direct_square), (map, long, double, cflow_direct_half))

cflow_direct_pipeline(cflow_direct_test_pipeline, int, &cmeta_type_int, double, 3,
                      CFlowDirectTestSteps);

#define CFlowDirectStatefulSteps(M)                                                                \
  CFlowDirectSteps(M, (map, int, long, cflow_direct_stateful_square))

cflow_direct_pipeline(cflow_direct_stateful_pipeline, int, &cmeta_type_int, long, 1,
                      CFlowDirectStatefulSteps);

#define CFlowDirectTrapSteps(M) CFlowDirectSteps(M, (map, int, long, cflow_direct_trap_map))

cflow_direct_pipeline(cflow_direct_trap_pipeline, int, &cmeta_type_int, long, 1,
                      CFlowDirectTrapSteps);

#define CFlowDirectChainSteps(M)                                                                   \
  CFlowDirectSteps(M, (map, int, int, cflow_direct_plus_one),                                      \
                   (map, int, int, cflow_direct_times_two))

cflow_direct_pipeline(cflow_direct_chain_pipeline, int, &cmeta_type_int, int, 2,
                      CFlowDirectChainSteps);

suite("CFlow Direct executor") {
  before_each() {
    memset(&cflow_direct_state, 0, sizeof(cflow_direct_state));
    cflow_direct_erased_invocations = 0u;
  }

  after_each() {
    cflow_result_destroy(&cflow_direct_state.kernel_result);
    cflow_result_destroy(&cflow_direct_state.plan_result);
    cflow_plan_destroy(&cflow_direct_state.plan);
    cflow_stream_destroy(&cflow_direct_state.stream);
  }

  it("matches Plan and Kernel for one generated linear value pipeline") {
    const int input[] = {1, 2, 3, 4, 5, 6};
    const double expected[] = {2.0, 8.0, 18.0};
    double direct_output[6] = {0};
    size_t direct_count = 0u;
    cflow_direct_fixture *state = &cflow_direct_state;

    check_true(cflow_direct_test_pipeline_build(&state->stream));
    check_true(cflow_plan_compile_surface(&state->plan, &state->stream.graph, NULL));
    check_true(cflow_plan_eval_array(&state->plan, input, 6u, &state->plan_result));
    check_true(cflow_eval_array(&state->stream.graph, input, 6u, &state->kernel_result));

    check_equal(cflow_direct_test_pipeline_eval_array(input, 6u, direct_output, 6u, &direct_count),
                CFLOW_DIRECT_OK);
    check_equal(direct_count, (size_t)3u);
    check_equal(direct_output, expected, sizeof(expected));
    check_equal(state->plan_result.count, direct_count);
    check_equal(state->plan_result.data, direct_output, direct_count * sizeof(direct_output[0]));
    check_true(cflow_result_equal(&state->plan_result, &state->kernel_result));
  }

  it("rejects insufficient capacity before writing output") {
    const int input[] = {1, 2, 3, 4, 5, 6};
    const double sentinel[] = {-1.0, -1.0, -1.0, -1.0, -1.0, -1.0};
    double output[] = {-1.0, -1.0, -1.0, -1.0, -1.0, -1.0};
    size_t output_count = 99u;

    check_equal(cflow_direct_test_pipeline_eval_array(input, 6u, output, 2u, &output_count),
                CFLOW_DIRECT_CAPACITY_EXCEEDED);
    check_equal(output_count, (size_t)0u);
    check_equal(output, sentinel, sizeof(sentinel));
  }

  it("accepts an empty input without data buffers") {
    size_t output_count = 99u;

    check_equal(cflow_direct_test_pipeline_eval_array(NULL, 0u, NULL, 0u, &output_count),
                CFLOW_DIRECT_OK);
    check_equal(output_count, (size_t)0u);
  }

  it("rejects overlapping input and output storage") {
    const int input[] = {1, 2, 3, 4, 5, 6};
    union {
      double alignment;
      unsigned char bytes[6u * sizeof(double)];
    } storage;
    unsigned char original[sizeof(storage.bytes)];
    size_t output_count = 0u;

    memset(storage.bytes, 0xa5, sizeof(storage.bytes));
    memcpy(storage.bytes, input, sizeof(input));
    memcpy(original, storage.bytes, sizeof(original));
    check_equal(cflow_direct_test_pipeline_eval_array((const int *)storage.bytes, 6u,
                                                      (double *)storage.bytes, 6u, &output_count),
                CFLOW_DIRECT_INVALID_ARGUMENT);
    check_equal(output_count, (size_t)0u);
    check_equal(storage.bytes, original, sizeof(original));
  }

  it("rejects nonempty null buffers and checked-size overflow") {
    int input = 2;
    double output = -1.0;
    size_t output_count = 99u;
    const size_t overflowing_count = SIZE_MAX / sizeof(int) + 1u;

    check_equal(cflow_direct_test_pipeline_eval_array(NULL, 1u, &output, 1u, &output_count),
                CFLOW_DIRECT_INVALID_ARGUMENT);
    check_equal(output, -1.0);

    check_equal(cflow_direct_test_pipeline_eval_array(&input, overflowing_count, &output,
                                                      overflowing_count, &output_count),
                CFLOW_DIRECT_INVALID_ARGUMENT);
    check_equal(output, -1.0);
  }

  it("rejects a stateful schema before graph construction or output writes") {
    const int input[] = {2, 4};
    const long sentinel[] = {-1L, -1L};
    long output[] = {-1L, -1L};
    size_t output_count = 99u;
    cflow_stream stream = {0};

    check_false(cflow_direct_stateful_pipeline_eligible());
    check_false(cflow_direct_stateful_pipeline_build(&stream));
    check_equal(cflow_direct_stateful_pipeline_eval_array(input, 2u, output, 2u, &output_count),
                CFLOW_DIRECT_INELIGIBLE);
    check_equal(output_count, (size_t)0u);
    check_equal(output, sentinel, sizeof(sentinel));
    cflow_stream_destroy(&stream);
  }

  it("calls the typed function directly without erased callback dispatch") {
    const int input[] = {1, 2, 3};
    const long expected[] = {3L, 6L, 9L};
    long output[3] = {0};
    size_t output_count = 0u;
    cflow_stream stream = {0};
    cflow_plan plan = {0};
    cflow_result plan_result = {0};
    cflow_result kernel_result = {0};
    cflow_plan_eval_stats stats = {0};

    check_true(cflow_direct_trap_pipeline_eligible());
    check_equal(cflow_direct_trap_pipeline_eval_array(input, 3u, output, 3u, &output_count),
                CFLOW_DIRECT_OK);
    check_equal(output_count, (size_t)3u);
    check_equal(output, expected, sizeof(expected));
    check_equal(cflow_direct_erased_invocations, (size_t)0u);

    check_true(cflow_direct_trap_pipeline_build(&stream));
    check_true(cflow_eval_array(&stream.graph, input, 3u, &kernel_result));
    check_equal(kernel_result.count, (size_t)3u);
    check_equal(kernel_result.data, expected, sizeof(expected));
    check_equal(cflow_direct_erased_invocations, (size_t)3u);

    check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
    check_true(cflow_plan_eval_array_profile(&plan, input, 3u, &plan_result, &stats));
    check_equal(plan_result.data, expected, sizeof(expected));
    check_equal(cflow_direct_erased_invocations, (size_t)6u);
    check_equal(stats.raw_batch_stage_calls, (size_t)0u);
    check_equal(stats.adapter_item_calls, (size_t)3u);

    cflow_result_destroy(&plan_result);
    cflow_plan_destroy(&plan);
    cflow_result_destroy(&kernel_result);
    cflow_stream_destroy(&stream);
  }

  it("chains same-type stages through the immediately preceding output") {
    const int input[] = {3};
    const int expected[] = {8};
    int output[] = {0};
    size_t output_count = 0u;
    cflow_stream stream = {0};
    cflow_result kernel_result = {0};

    check_true(cflow_direct_chain_pipeline_build(&stream));
    check_equal(cflow_direct_chain_pipeline_eval_array(input, 1u, output, 1u, &output_count),
                CFLOW_DIRECT_OK);
    check_equal(output_count, (size_t)1u);
    check_equal(output, expected, sizeof(expected));
    check_true(cflow_eval_array(&stream.graph, input, 1u, &kernel_result));
    check_equal(kernel_result.data, expected, sizeof(expected));

    cflow_result_destroy(&kernel_result);
    cflow_stream_destroy(&stream);
  }
}
