#include "tinytest.h"

#include <cflow/cflow.h>

#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

static _Atomic size_t cflow_parallel_prefix_calls;

typed(reduce, associative, long, cflow_parallel_left,
      (long left, long right)) {
  (void)right;
  return left;
}

typed(map, value, long, cflow_parallel_widen, (int value)) {
  atomic_fetch_add(&cflow_parallel_prefix_calls, 1u);
  return (long)value * 2L;
}

typedef struct cflow_parallel_reduce_fixture {
  cflow_stream stream;
  cflow_plan plan;
  cflow_executor executor;
  cflow_result result;
  cflow_plan_eval_options options;
} cflow_parallel_reduce_fixture;

static cflow_parallel_reduce_fixture cflow_parallel_state;

static bool cflow_parallel_fixture_init(cflow_parallel_reduce_fixture *state) {
  if (!state || !cflow_stream_init(&state->stream, &cmeta_type_long) ||
      !state->stream.reduce(&state->stream, cflow_parallel_left) ||
      !cflow_plan_compile_surface(&state->plan, &state->stream.graph, NULL) ||
      !cflow_executor_worker_init_with_capacity(&state->executor, 4u, 16u))
    return false;
  state->options = (cflow_plan_eval_options){
      .mode = CFLOW_PLAN_EXECUTION_PARALLEL_REDUCE,
      .executor = &state->executor,
      .max_tasks = 4u,
      .min_items_per_task = 2u};
  return true;
}

suite("CFlow ordered parallel reduce") {
  before_each() {
    memset(&cflow_parallel_state, 0, sizeof(cflow_parallel_state));
    check_true(cflow_parallel_fixture_init(&cflow_parallel_state));
  }

  after_each() {
    cflow_result_destroy(&cflow_parallel_state.result);
    cflow_executor_destroy(&cflow_parallel_state.executor);
    cflow_plan_destroy(&cflow_parallel_state.plan);
    cflow_stream_destroy(&cflow_parallel_state.stream);
  }

  it("rejects empty and one-item inputs without sequential fallback") {
    const long one[] = {41L};
    cflow_parallel_reduce_fixture *state = &cflow_parallel_state;

    check_false(cflow_plan_eval_array_with_options(
        &state->plan, NULL, 0u, &state->options, &state->result));
    check_null(state->result.data);
    check_equal(state->result.count, (size_t)0u);

    check_false(cflow_plan_eval_array_with_options(
        &state->plan, one, 1u, &state->options, &state->result));
    check_null(state->result.data);
    check_equal(state->result.count, (size_t)0u);
  }

  it("rejects invalid options and size overflow before committing output") {
    const long input[] = {61L, 62L, 63L, 64L};
    cflow_parallel_reduce_fixture *state = &cflow_parallel_state;
    cflow_result untouched = {
        .data = (void *)input,
        .count = 99u,
        .type = &cmeta_type_long
    };

    state->options.max_tasks = 0u;
    check_false(cflow_plan_eval_array_with_options(
        &state->plan, input, 4u, &state->options, &untouched));
    check_true(untouched.data == input);
    check_equal(untouched.count, (size_t)99u);

    state->options.max_tasks = 4u;
    state->options.min_items_per_task = 0u;
    check_false(cflow_plan_eval_array_with_options(
        &state->plan, input, 4u, &state->options, &untouched));
    check_true(untouched.data == input);

    state->options.min_items_per_task = 1u;
    check_false(cflow_plan_eval_array_with_options(
        &state->plan, input, SIZE_MAX, &state->options, &untouched));
    check_true(untouched.data == input);
  }

  it("preserves encounter order for exact and remainder chunks") {
    const long exact[] = {11L, 12L, 13L, 14L, 15L, 16L, 17L, 18L};
    const long remainder[] = {21L, 22L, 23L, 24L, 25L, 26L, 27L};
    cflow_parallel_reduce_fixture *state = &cflow_parallel_state;

    check_true(cflow_plan_eval_array_with_options(
        &state->plan, exact, 8u, &state->options, &state->result));
    check_equal(state->result.count, (size_t)1u);
    check_true(cmeta_type_equal(state->result.type, &cmeta_type_long));
    check_equal(*(const long *)state->result.data, 11L);
    cflow_result_destroy(&state->result);

    state->options.max_tasks = 3u;
    check_true(cflow_plan_eval_array_with_options(
        &state->plan, remainder, 7u, &state->options, &state->result));
    check_equal(state->result.count, (size_t)1u);
    check_equal(*(const long *)state->result.data, 21L);
  }

  it("supports repeated evaluation without retaining task state") {
    const long first[] = {31L, 32L, 33L, 34L};
    const long second[] = {51L, 52L, 53L, 54L, 55L};
    cflow_parallel_reduce_fixture *state = &cflow_parallel_state;

    check_true(cflow_plan_eval_array_with_options(
        &state->plan, first, 4u, &state->options, &state->result));
    check_equal(*(const long *)state->result.data, 31L);
    cflow_result_destroy(&state->result);

    check_true(cflow_plan_eval_array_with_options(
        &state->plan, second, 5u, &state->options, &state->result));
    check_equal(*(const long *)state->result.data, 51L);
  }

  it("materializes the sequential prefix exactly once before splitting") {
    const int input[] = {7, 8, 9, 10, 11, 12};
    cflow_stream stream = {0};
    cflow_plan plan = {0};
    cflow_result result = {0};
    cflow_parallel_reduce_fixture *state = &cflow_parallel_state;

    atomic_store(&cflow_parallel_prefix_calls, 0u);
    check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
    check_not_null(stream.map(&stream, cflow_parallel_widen));
    check_not_null(stream.reduce(&stream, cflow_parallel_left));
    check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
    check_true(cflow_plan_parallel_reduce_supported(&plan));
    check_true(cflow_plan_eval_array_with_options(
        &plan, input, 6u, &state->options, &result));
    check_equal(result.count, (size_t)1u);
    check_equal(*(const long *)result.data, 14L);
    check_equal(atomic_load(&cflow_parallel_prefix_calls), (size_t)6u);

    cflow_result_destroy(&result);
    cflow_plan_destroy(&plan);
    cflow_stream_destroy(&stream);
  }
}
