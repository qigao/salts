#include <cflow/cflow.h>
#include <cflow/plan_internal.h>
#include "tinytest.h"

#include "cflow_test_ops.h"

typedef struct cflow_test_range_owner {
    uint64_t generation;
    int value;
} cflow_test_range_owner;

static uint64_t cflow_test_range_version(const void *object) {
    return ((const cflow_test_range_owner *)object)->generation;
}

static cmeta_gen_status cflow_test_range_next(const void *object,
                                               cmeta_range_cursor *cursor,
                                               void *out_value) {
    const cflow_test_range_owner *owner =
        (const cflow_test_range_owner *)object;
    if (cursor->index != 0u)
        return CMETA_GEN_DONE;
    *(int *)out_value = owner->value;
    ++cursor->index;
    return CMETA_GEN_VALUE;
}

static bool cflow_test_build_pipeline(cflow_stream *stream) {
    return cflow_stream_init(stream, &cmeta_type_int) &&
           stream->filter(stream, cflow_test_even) &&
           stream->map(stream, cflow_test_square) &&
           stream->map(stream, cflow_test_half);
}

typed(map, stateful, long, cflow_test_stateful_add_ten, (int value)) {
    return (long)value + 10L;
}

static void cflow_test_check_expected(const cflow_result *result) {
    const double *values;

    check_not_null(result);
    check_equal(result->count, (size_t)3);
    check_true(cmeta_type_equal(result->type, &cmeta_type_double));
    check_not_null(result->data);
    values = result->data;
    check_equal(values[0], 2.0);
    check_equal(values[1], 8.0);
    check_equal(values[2], 18.0);
}

suite("CFlow pipeline") {
    it("reports a mutated borrowed range owner") {
        cflow_test_range_owner owner = {7u, 42};
        cmeta_range range = {
            &owner, &cmeta_type_int, CMETA_RANGE_NONE, NULL,
            cflow_test_range_next, owner.generation, cflow_test_range_version
        };
        cflow_source source = {0};
        cflow_step step;
        int output = 0;

        check_true(cflow_source_from_range(&source, range));
        step = cflow_source_resume(&source, NULL, &output);
        check_true(step.kind == CFLOW_STEP_VALUE);
        check_equal(output, 42);
        ++owner.generation;
        step = cflow_source_resume(&source, NULL, &output);
        check_true(step.kind == CFLOW_STEP_ERROR);
        check_equal(step.error, "range owner mutated");
        cflow_source_destroy(&source);
    }

    it("evaluates a typed fluent pipeline") {
        cflow_stream stream = {0};
        cflow_result result = {0};
        const int input[] = {1, 2, 3, 4, 5, 6};

        check_true(cflow_test_build_pipeline(&stream));
        check_true(cflow_stream_ok(&stream));
        check_true(cflow_eval_array(&stream.graph, input, 6u, &result));
        cflow_test_check_expected(&result);

        cflow_result_destroy(&result);
        cflow_stream_destroy(&stream);
    }

    it("keeps compiled-plan output equal to interpreter output") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_plan_compile_stats stats = {0};
        cflow_result interpreted = {0};
        cflow_result compiled = {0};
        const int input[] = {1, 2, 3, 4, 5, 6};

        check_true(cflow_test_build_pipeline(&stream));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, &stats));
        check_null(plan.error);
        check_true(cflow_eval_array(&stream.graph, input, 6u, &interpreted));
        check_true(cflow_plan_eval_array(&plan, input, 6u, &compiled));
        check_true(cflow_result_equal(&interpreted, &compiled));
        check_equal(stats.instructions, (size_t)2);
        check_equal(stats.map_callbacks, (size_t)2);
        cflow_test_check_expected(&compiled);

        cflow_result_destroy(&compiled);
        cflow_result_destroy(&interpreted);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("reports exact bounded resources for a fused value plan") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_result result = {0};
        cflow_plan_eval_stats stats = {0};
        const int input[] = {1, 2, 3, 4, 5, 6};
        const size_t expected_intermediate = 3u * sizeof(long);
        const size_t expected_result = 3u * sizeof(double);
        const size_t expected_total = 1u + expected_intermediate + expected_result;

        check_true(cflow_test_build_pipeline(&stream));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_true(cflow_plan_eval_array_profile(&plan, input, 6u, &result, &stats));
        cflow_test_check_expected(&result);
        check_true(stats.fused_value_path);
        check_equal(stats.allocation_calls, (size_t)3u);
        check_equal(stats.selection_bytes, (size_t)1u);
        check_equal(stats.intermediate_bytes, expected_intermediate);
        check_equal(stats.result_bytes, expected_result);
        check_equal(stats.allocated_bytes, expected_total);
        check_equal(stats.peak_live_bytes, expected_intermediate + expected_result);
        check_equal(stats.staged_input_copy_bytes, (size_t)0u);

        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("retains the materialized path for a stateful map") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_result result = {0};
        cflow_plan_eval_stats stats = {0};
        const int input[] = {1, 2, 3};
        const long expected[] = {11L, 12L, 13L};

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.map(&stream, cflow_test_stateful_add_ten));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_true(cflow_plan_eval_array_profile(&plan, input, 3u, &result, &stats));
        check_false(stats.fused_value_path);
        check_equal(result.count, (size_t)3u);
        check_true(cmeta_type_equal(result.type, &cmeta_type_long));
        check_equal(result.data, expected, sizeof(expected));

        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("returns a typed null result without allocations for empty fused input") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_result result = {0};
        cflow_plan_eval_stats stats = {0};

        check_true(cflow_test_build_pipeline(&stream));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_true(cflow_plan_eval_array_profile(&plan, NULL, 0u, &result, &stats));
        check_true(stats.fused_value_path);
        check_equal(stats.allocation_calls, (size_t)0u);
        check_equal(stats.allocated_bytes, (size_t)0u);
        check_equal(stats.peak_live_bytes, (size_t)0u);
        check_equal(result.count, (size_t)0u);
        check_true(cmeta_type_equal(result.type, &cmeta_type_double));
        check_null(result.data);

        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("fails a fused transaction before allocation for a missing input buffer") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        int sentinel = 0;
        cflow_result result = {&sentinel, 1u, &cmeta_type_int};
        cflow_plan_eval_stats stats = {0};

        check_true(cflow_test_build_pipeline(&stream));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_false(cflow_plan_eval_array_profile(&plan, NULL, 1u, &result, &stats));
        check_true(stats.fused_value_path);
        check_equal(stats.allocation_calls, (size_t)0u);
        check_equal(result.count, (size_t)0u);
        check_null(result.type);
        check_null(result.data);

        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("allocates no result storage when a fused filter rejects every input") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_result result = {0};
        cflow_plan_eval_stats stats = {0};
        const int input[] = {1, 3, 5};
        const size_t expected_auxiliary = 1u;

        check_true(cflow_test_build_pipeline(&stream));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_true(cflow_plan_eval_array_profile(&plan, input, 3u, &result, &stats));
        check_true(stats.fused_value_path);
        check_equal(stats.allocation_calls, (size_t)1u);
        check_equal(stats.selection_bytes, (size_t)1u);
        check_equal(stats.intermediate_bytes, (size_t)0u);
        check_equal(stats.result_bytes, (size_t)0u);
        check_equal(stats.allocated_bytes, expected_auxiliary);
        check_equal(stats.peak_live_bytes, expected_auxiliary);
        check_equal(result.count, (size_t)0u);
        check_true(cmeta_type_equal(result.type, &cmeta_type_double));
        check_null(result.data);

        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("uses exact intermediate and result buffers for a map-only plan") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_result result = {0};
        cflow_plan_eval_stats stats = {0};
        const int input[] = {1, 2, 3};
        const double expected[] = {0.5, 2.0, 4.5};
        const size_t expected_intermediate = 3u * sizeof(long);
        const size_t expected_result = sizeof(expected);

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.map(&stream, cflow_test_square));
        check_not_null(stream.map(&stream, cflow_test_half));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_true(cflow_plan_eval_array_profile(&plan, input, 3u, &result, &stats));
        check_true(stats.fused_value_path);
        check_equal(stats.allocation_calls, (size_t)2u);
        check_equal(stats.selection_bytes, (size_t)0u);
        check_equal(stats.intermediate_bytes, expected_intermediate);
        check_equal(stats.result_bytes, expected_result);
        check_equal(stats.allocated_bytes, expected_intermediate + expected_result);
        check_equal(stats.peak_live_bytes, expected_intermediate + expected_result);
        check_equal(result.count, (size_t)3u);
        check_true(cmeta_type_equal(result.type, &cmeta_type_double));
        check_equal(result.data, expected, sizeof(expected));

        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("copies only selected values for a filter-only plan") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_result result = {0};
        cflow_plan_eval_stats stats = {0};
        const int input[] = {1, 2, 3, 4};
        const int expected[] = {2, 4};
        const size_t expected_total = 1u + sizeof(expected);

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.filter(&stream, cflow_test_even));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_true(cflow_plan_eval_array_profile(&plan, input, 4u, &result, &stats));
        check_true(stats.fused_value_path);
        check_equal(stats.allocation_calls, (size_t)2u);
        check_equal(stats.selection_bytes, (size_t)1u);
        check_equal(stats.intermediate_bytes, (size_t)0u);
        check_equal(stats.result_bytes, sizeof(expected));
        check_equal(stats.allocated_bytes, expected_total);
        check_equal(stats.peak_live_bytes, expected_total);
        check_equal(result.count, (size_t)2u);
        check_true(cmeta_type_equal(result.type, &cmeta_type_int));
        check_equal(result.data, expected, sizeof(expected));

        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("predecodes immutable Filter and Map callback records once") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        const cflow_plan_impl *impl;
        const cflow_plan_inst *filter;
        const cflow_plan_inst *map;

        check_true(cflow_test_build_pipeline(&stream));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        impl = (const cflow_plan_impl *)plan.impl;
        check_not_null(impl);
        check_equal(impl->count, (size_t)2u);

        filter = &impl->code[0];
        check_equal(filter->opcode, CMETA_PLAN_FILTER);
        check_not_null(filter->call.invoke);
        check_true(cmeta_type_equal(filter->call.input_type, &cmeta_type_int));
        check_true(cmeta_type_equal(filter->call.output_type, &cmeta_type_bool));

        map = &impl->code[1];
        check_equal(map->opcode, CMETA_PLAN_MAP);
        check_equal(map->fn_chain_count, (size_t)2u);
        check_not_null(map->fn_chain[0].invoke);
        check_true(cmeta_type_equal(map->fn_chain[0].input_type, &cmeta_type_int));
        check_true(cmeta_type_equal(map->fn_chain[0].output_type, &cmeta_type_long));
        check_not_null(map->fn_chain[1].invoke);
        check_true(cmeta_type_equal(map->fn_chain[1].input_type, &cmeta_type_long));
        check_true(cmeta_type_equal(map->fn_chain[1].output_type, &cmeta_type_double));

        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("compares result values with their CMeta equality traits") {
        float positive_zero = 0.0f;
        float negative_zero = -0.0f;
        const cflow_result left = {
            .data = &positive_zero, .count = 1u, .type = &cmeta_type_float
        };
        const cflow_result right = {
            .data = &negative_zero, .count = 1u, .type = &cmeta_type_float
        };

        check_true(cflow_result_equal(&left, &right));
    }

    it("verifies normalization optimization and execution parity") {
        cflow_stream stream = {0};
        cflow_verify_report report = {0};
        const int input[] = {1, 2, 3, 4, 5, 6};

        check_true(cflow_test_build_pipeline(&stream));
        check_true(cflow_verify_pipeline(&stream.graph, input, 6u, &report));
        check_null(report.error);
        check_equal(report.input_count, (size_t)6);
        check_equal(report.output_count, (size_t)3);
        check_true(report.compiled_plan_checked);
        check_equal(report.compiled_instructions, (size_t)2);

        cflow_stream_destroy(&stream);
    }

    it("rejects missing stream initialization arguments") {
        cflow_stream stream = {0};

        check_null(cflow_stream_init(NULL, &cmeta_type_int));
        check_null(cflow_stream_init(&stream, NULL));
    }
}
