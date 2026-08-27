#include <cflow/cflow.h>

#include "adapters_internal.h"

#include "tinytest.h"

#include <stdint.h>

typed(filter, value, bool, cflow_count_even, (int value)) {
    return value % 2 == 0;
}

typed(map, value, long, cflow_count_widen, (int value)) {
    return (long)value;
}

typedef struct count_range_owner {
    const int *values;
    size_t count;
    size_t fail_at;
} count_range_owner;

static size_t count_range_size(const void *object) {
    const count_range_owner *owner = (const count_range_owner *)object;

    return owner ? owner->count : 0u;
}

static cmeta_gen_status count_range_next(const void *object,
                                         cmeta_range_cursor *cursor,
                                         void *out_value) {
    const count_range_owner *owner = (const count_range_owner *)object;

    if (!owner || !cursor || !out_value)
        return CMETA_GEN_ERROR;
    if (cursor->index == owner->fail_at)
        return CMETA_GEN_ERROR;
    if (cursor->index >= owner->count)
        return CMETA_GEN_DONE;
    *(int *)out_value = owner->values[cursor->index++];
    return CMETA_GEN_VALUE;
}

static cmeta_range count_range(const count_range_owner *owner) {
    return (cmeta_range){
        .object = owner,
        .element_type = &cmeta_type_int,
        .flags = CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED |
                 CMETA_RANGE_REUSABLE,
        .size = count_range_size,
        .next = count_range_next,
        .version = 0u,
        .current_version = NULL
    };
}

static cmeta_gen_status unbounded_count_range_next(
    const void *object, cmeta_range_cursor *cursor, void *out_value) {
    if (!object || !cursor || !out_value)
        return CMETA_GEN_ERROR;
    *(int *)out_value = (int)cursor->index++;
    return CMETA_GEN_VALUE;
}

spec("CFlow Stream count terminal") {
    it("counts empty and transformed interpreted streams") {
        const int values[] = {1, 2, 3, 4, 5, 6};
        const count_range_owner empty = {values, 0u, SIZE_MAX};
        const count_range_owner input = {values, 6u, SIZE_MAX};
        cflow_stream stream = {0};
        const char *error = "stale";
        size_t count = 99u;

        check_not_null(cflow_stream_from_range(&stream, count_range(&empty)));
        check_true(cflow_eval_count(&stream, &count, &error));
        check_equal(count, (size_t)0u);
        check_null(error);
        cflow_stream_destroy(&stream);

        check_not_null(cflow_stream_from_range(&stream, count_range(&input)));
        check_not_null(stream.filter(&stream, cflow_count_even)
                                    ->skip(&stream, 1u)
                                    ->take(&stream, 2u));
        check_true(cflow_eval_count(&stream, &count, &error));
        check_equal(count, (size_t)2u);
        check_null(error);
        cflow_stream_destroy(&stream);
    }

    it("preserves operator order and output type changes") {
        const int values[] = {1, 2, 3, 4};
        const count_range_owner input = {values, 4u, SIZE_MAX};
        cflow_stream stream = {0};
        const char *error = NULL;
        size_t count = 0u;

        check_not_null(cflow_stream_from_range(&stream, count_range(&input)));
        check_not_null(stream.take(&stream, 2u)
                                    ->filter(&stream, cflow_count_even)
                                    ->map(&stream, cflow_count_widen));
        check_true(cmeta_type_equal(
            cflow_stream_output_type(&stream), &cmeta_type_long));
        check_true(cflow_eval_count(&stream, &count, &error));
        check_equal(count, (size_t)1u);
        check_null(error);

        cflow_stream_destroy(&stream);
    }

    it("uses independent state for repeated evaluation") {
        const int values[] = {2, 4, 6};
        const count_range_owner input = {values, 3u, SIZE_MAX};
        cflow_stream stream = {0};
        const char *error = NULL;
        size_t count = 0u;

        check_not_null(cflow_stream_from_range(&stream, count_range(&input)));
        check_true(cflow_eval_count(&stream, &count, &error));
        check_equal(count, (size_t)3u);
        count = 91u;
        check_true(cflow_eval_count(&stream, &count, &error));
        check_equal(count, (size_t)3u);
        check_null(error);

        cflow_stream_destroy(&stream);
    }

    it("terminates an unbounded source when take supplies the bound") {
        const int source_identity = 0;
        const cmeta_range input = {
            .object = &source_identity,
            .element_type = &cmeta_type_int,
            .flags = CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE,
            .size = NULL,
            .next = unbounded_count_range_next,
            .version = 0u,
            .current_version = NULL
        };
        cflow_stream stream = {0};
        const char *error = NULL;
        size_t count = 0u;

        check_not_null(cflow_stream_from_range(&stream, input));
        check_not_null(stream.take(&stream, 3u));
        check_true(cflow_eval_count(&stream, &count, &error));
        check_equal(count, (size_t)3u);
        check_null(error);

        cflow_stream_destroy(&stream);
    }

    it("rolls output back on invalid input and range errors") {
        const int values[] = {3, 5, 7};
        const count_range_owner input = {values, 3u, 1u};
        cflow_stream stream = {0};
        const char *error = "stale";
        size_t count = 99u;

        check_false(cflow_eval_count(NULL, &count, &error));
        check_equal(count, (size_t)0u);
        check_equal(error, "invalid stream count arguments");

        check_not_null(cflow_stream_from_range(&stream, count_range(&input)));
        count = 99u;
        error = "stale";
        check_false(cflow_eval_count(&stream, &count, &error));
        check_equal(count, (size_t)0u);
        check_equal(error, "range iteration failed");

        cflow_stream_destroy(&stream);
    }

    it("rejects failed Streams and Range admission errors") {
        const int values[] = {1, 2, 3};
        const count_range_owner input = {values, 3u, SIZE_MAX};
        cmeta_type_desc missing_traits = cmeta_type_int;
        cmeta_range rejected_range = count_range(&input);
        cflow_stream stream = {0};
        const char *error = NULL;
        size_t count = 99u;

        check_not_null(cflow_stream_from_range(&stream, count_range(&input)));
        check_not_null(stream.filter(&stream, (cflow_filter_callable){0}));
        check_false(cflow_stream_ok(&stream));
        check_false(cflow_eval_count(&stream, &count, &error));
        check_equal(count, (size_t)0u);
        check_equal(
            error, "callable signature/effect/property contract is invalid");
        cflow_stream_destroy(&stream);

        missing_traits.traits = NULL;
        rejected_range.element_type = &missing_traits;
        check_not_null(cflow_stream_from_range(&stream, rejected_range));
        count = 99u;
        error = NULL;
        check_false(cflow_eval_count(&stream, &count, &error));
        check_equal(count, (size_t)0u);
        check_equal(
            error, "range element type lacks required lifecycle traits");
        cflow_stream_destroy(&stream);
    }

    it("distinguishes exact bounded completion from overflow") {
        const int exact_values[] = {1, 2, 3};
        const int overflow_values[] = {1, 2, 3, 4};
        const count_range_owner exact = {
            exact_values, 3u, SIZE_MAX
        };
        const count_range_owner overflow = {
            overflow_values, 4u, SIZE_MAX
        };
        cflow_stream stream = {0};
        const char *error = NULL;
        size_t count = 99u;

        check_not_null(cflow_stream_from_range(&stream, count_range(&exact)));
        check_true(cflow_eval_count_bounded(
            &stream, 3u, &count, &error));
        check_equal(count, (size_t)3u);
        check_null(error);
        cflow_stream_destroy(&stream);

        check_not_null(
            cflow_stream_from_range(&stream, count_range(&overflow)));
        count = 99u;
        error = NULL;
        check_false(cflow_eval_count_bounded(
            &stream, 3u, &count, &error));
        check_equal(count, (size_t)0u);
        check_equal(error, "stream count overflow");
        cflow_stream_destroy(&stream);
    }

    it("rejects a Graph that cannot be normalized") {
        const int values[] = {1};
        const count_range_owner input = {values, 1u, SIZE_MAX};
        cflow_stream stream = {0};
        const char *error = NULL;
        size_t count = 99u;

        check_not_null(cflow_stream_from_range(&stream, count_range(&input)));
        stream.graph.root = CMETA_INVALID_ID;
        check_false(cflow_eval_count(&stream, &count, &error));
        check_equal(count, (size_t)0u);
        check_equal(error, "graph normalization failed");

        cflow_stream_destroy(&stream);
    }
}
