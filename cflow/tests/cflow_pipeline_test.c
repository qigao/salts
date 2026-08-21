#include <cflow/cflow.h>
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
