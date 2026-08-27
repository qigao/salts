#include <cflow/cflow.h>

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
} count_range_owner;

static size_t count_range_size(const void *object) {
    const count_range_owner *owner = (const count_range_owner *)object;

    return owner ? owner->count : 0u;
}

static cmeta_gen_status count_range_next(const void *object,
                                         cmeta_range_cursor *cursor,
                                         void *out_value) {
    const count_range_owner *owner = (const count_range_owner *)object;

    if (!owner || !cursor || !out_value) return CMETA_GEN_ERROR;
    if (cursor->index >= owner->count) return CMETA_GEN_DONE;
    *(int *)out_value = owner->values[cursor->index++];
    return cursor->index == owner->count
        ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE;
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
    if (!object || !cursor || !out_value) return CMETA_GEN_ERROR;
    *(int *)out_value = (int)cursor->index++;
    return CMETA_GEN_VALUE;
}

spec("CFlow Stream count regression") {
    it("preserves operator order and output type changes") {
        const int values[] = {1, 2, 3, 4};
        const count_range_owner input = {values, 4u};
        cflow_stream stream = {0};
        const char *error = NULL;
        size_t count = 99u;

        check_not_null(cflow_stream_from_range(&stream, count_range(&input)));
        check_not_null(stream.take(&stream, 2u)
                                    ->filter(&stream, cflow_count_even)
                                    ->map(&stream, cflow_count_widen));
        check_true(cmeta_type_equal(
            cflow_stream_output_type(&stream), &cmeta_type_long));
        check_true(cflow_stream_count(&stream, &count, &error));
        check_equal(count, (size_t)1u);
        check_null(error);

        cflow_stream_destroy(&stream);
    }

    it("uses independent state for repeated evaluation") {
        const int values[] = {2, 4, 6};
        const count_range_owner input = {values, 3u};
        cflow_stream stream = {0};
        const char *error = NULL;
        size_t count = 0u;

        check_not_null(cflow_stream_from_range(&stream, count_range(&input)));
        check_true(cflow_stream_count(&stream, &count, &error));
        check_equal(count, (size_t)3u);
        count = 91u;
        check_true(cflow_stream_count(&stream, &count, &error));
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
        check_true(cflow_stream_count(&stream, &count, &error));
        check_equal(count, (size_t)3u);
        check_null(error);

        cflow_stream_destroy(&stream);
    }
}
