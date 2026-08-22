#include <cmeta/struct.h>

Struct(StreamStudent,
    (int, student_id),
    (int, class_id),
    (int, age)
);

#include <turbostl/stream.h>

#include "tinytest.h"

#include <string.h>

typed(List, StreamInts, int);
typed(List, StreamLongs, long);
typed(List, StreamLongList, long);
typed(Map, StreamClassAges, int, int);

typed(filter, value, bool, stream_keep_even, (int value)) {
    return value % 2 == 0;
}

typed(map, value, long, stream_square, (int value)) {
    return (long)value * (long)value;
}

typed(map, value, long, stream_age_as_long, (int age)) {
    return (long)age;
}

typed(reduce, associative, long, stream_sum_age, (long left, long right)) {
    return left + right;
}

static bool stream_test_input(StreamInts *input) {
    size_t index;

    if (StreamInts_init(input, 6u) != TURBO_STL_OK)
        return false;
    for (index = 1u; index <= 6u; ++index) {
        if (StreamInts_push_back(input, (int)index) != TURBO_STL_OK) {
            StreamInts_destroy(input);
            return false;
        }
    }
    return true;
}

suite("TurboSTL CFlow Stream") {
    it("collects a Java-style fluent pipeline without naming generated collectors") {
        StreamInts input = {0};
        StreamLongList output = {0};
        turbostl_stream_t pipeline = {0};
        turbostl_collect_result result;
        long value = 0;

        check_true(stream_test_input(&input));
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.filter(&pipeline, stream_keep_even)
                                   ->map(&pipeline, stream_square));
        result = to_list(&pipeline, StreamLongList, &output, 3u);
        check_equal(result.status, CMETA_OK);
        check_null(result.error);
        check_true(result.ok);
        check_equal(result.count, (size_t)3u);
        check_equal(StreamLongList_pop_front(&output, &value), TURBO_STL_OK);
        check_equal(value, 4L);
        check_equal(StreamLongList_pop_front(&output, &value), TURBO_STL_OK);
        check_equal(value, 16L);
        check_equal(StreamLongList_pop_front(&output, &value), TURBO_STL_OK);
        check_equal(value, 36L);

        StreamLongList_destroy(&output);
        turbostl_stream_destroy(&pipeline);
        StreamInts_destroy(&input);
    }

    it("collects an owned generic array through the CFlow terminal") {
        StreamInts input = {0};
        turbostl_stream_t pipeline = {0};
        cflow_result output = {0};

        check_true(stream_test_input(&input));
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.filter(&pipeline, stream_keep_even)
                                   ->map(&pipeline, stream_square));
        check_true(to_array(&pipeline, 3u, &output));
        check_equal(output.count, (size_t)3u);
        check_equal(((const long *)output.data)[0], 4L);
        check_equal(((const long *)output.data)[1], 16L);
        check_equal(((const long *)output.data)[2], 36L);

        cflow_result_destroy(&output);
        turbostl_stream_destroy(&pipeline);
        StreamInts_destroy(&input);
    }

    it("rejects an array result that exceeds its explicit limit") {
        StreamInts input = {0};
        turbostl_stream_t pipeline = {0};
        cflow_result output = {0};

        check_true(stream_test_input(&input));
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.filter(&pipeline, stream_keep_even)
                                   ->map(&pipeline, stream_square));
        check_false(to_array(&pipeline, 2u, &output));
        check_null(output.data);
        check_equal(output.count, (size_t)0u);
        check_null(output.type);

        turbostl_stream_destroy(&pipeline);
        StreamInts_destroy(&input);
    }

    it("aborts collection when the borrowed source mutates") {
        StreamInts input = {0};
        StreamLongs output = {0};
        const StreamLongs zero = {0};
        turbostl_stream_t pipeline = {0};
        turbostl_collect_result result;

        check_true(stream_test_input(&input));
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.map(&pipeline, stream_square));
        check_equal(StreamInts_push_back(&input, 7), TURBO_STL_CAPACITY_EXCEEDED);
        StreamInts_clear(&input);
        result = collect(&pipeline, StreamLongs, &output, 6u);
        check_false(result.ok);
        check_equal(result.error, "range owner mutated");
        check_equal(result.status, CMETA_CALLBACK_ERROR);
        check_equal(memcmp(&output, &zero, sizeof(output)), 0);

        turbostl_stream_destroy(&pipeline);
        StreamInts_destroy(&input);
    }

    it("keeps the output zero when its collection limit is exceeded") {
        StreamInts input = {0};
        StreamLongs output = {0};
        const StreamLongs zero = {0};
        turbostl_stream_t pipeline = {0};
        turbostl_collect_result result;

        check_true(stream_test_input(&input));
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.filter(&pipeline, stream_keep_even)
                                   ->map(&pipeline, stream_square));
        result = collect(&pipeline, StreamLongs, &output, 2u);
        check_false(result.ok);
        check_equal(result.error, "observer rejected value");
        check_equal(result.status, CMETA_CAPACITY_EXCEEDED);
        check_equal(memcmp(&output, &zero, sizeof(output)), 0);

        turbostl_stream_destroy(&pipeline);
        StreamInts_destroy(&input);
    }

    it("streams reflected students and computes their class average age") {
        static const StreamStudent students[] = {
            {101, 7, 18},
            {102, 7, 20},
            {103, 7, 22}
        };
        StreamClassAges ages = {0};
        turbostl_stream_t pipeline = {0};
        cflow_result total = {0};
        size_t student_count;

        check_equal(FieldCount(StreamStudent), (size_t)3u);
        check_not_null(FieldFind(StreamStudent, "age"));
        check_equal(StreamClassAges_init(&ages, 3u), TURBO_STL_OK);
        check_equal(StreamClassAges_put(&ages, students[0].student_id,
                                        students[0].age),
                    TURBO_STL_OK);
        check_equal(StreamClassAges_put(&ages, students[1].student_id,
                                        students[1].age),
                    TURBO_STL_OK);
        check_equal(StreamClassAges_put(&ages, students[2].student_id,
                                        students[2].age),
                    TURBO_STL_OK);
        student_count = StreamClassAges_size(&ages);
        check_not_null(stream_values(&ages, &pipeline));
        check_not_null(pipeline.map(&pipeline, stream_age_as_long)
                                   ->reduce(&pipeline, stream_sum_age));
        check_true(to_array(&pipeline, 1u, &total));
        check_equal(total.count, (size_t)1u);
        check_equal(*(const long *)total.data, 60L);
        check_equal((double)*(const long *)total.data / (double)student_count, 20.0);

        cflow_result_destroy(&total);
        turbostl_stream_destroy(&pipeline);
        StreamClassAges_destroy(&ages);
    }
}
