#include <cmeta/struct.h>

Struct(StreamStudent,
    (int, student_id),
    (int, class_id),
    (int, age)
);

#include <turbostl/stream.h>

#include "tinytest.h"

typed(List, StreamIntList, int);
typed(List, StreamLongList, long);
typed(Map, StreamAgeMap, int, int);

typedef cmeta_collector (*StreamLongListCollector)(StreamLongList *, size_t);
typedef cmeta_collector (*StreamAgeMapCollector)(StreamAgeMap *, size_t);
_Static_assert(
    _Generic(&StreamLongList_collector,
             StreamLongListCollector: 1,
             default: 0),
    "generated collector must require its concrete output wrapper type");
_Static_assert(
    _Generic(&StreamAgeMap_collector,
             StreamAgeMapCollector: 1,
             default: 0),
    "associative collector must require its concrete output wrapper type");

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

static bool stream_test_input(StreamIntList *input) {
    size_t index;

    if (list_init(StreamIntList, input, 6u) != STL_OK)
        return false;
    for (index = 1u; index <= 6u; ++index) {
        int value = (int)index;
        if (list_add(StreamIntList, input, value) != STL_OK) {
            list_destroy(StreamIntList, input);
            return false;
        }
    }
    return true;
}

suite("TurboSTL CFlow Stream") {
    it("collects a fluent pipeline into an explicitly typed output") {
        StreamIntList input = {0};
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
        check_equal(list_pop_front(StreamLongList, &output, &value), STL_OK);
        check_equal(value, 4L);
        check_equal(list_pop_front(StreamLongList, &output, &value), STL_OK);
        check_equal(value, 16L);
        check_equal(list_pop_front(StreamLongList, &output, &value), STL_OK);
        check_equal(value, 36L);

        list_destroy(StreamLongList, &output);
        turbostl_stream_destroy(&pipeline);
        list_destroy(StreamIntList, &input);
    }

    it("collects an owned generic array through the CFlow terminal") {
        StreamIntList input = {0};
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
        list_destroy(StreamIntList, &input);
    }

    it("rejects an array result that exceeds its explicit limit") {
        StreamIntList input = {0};
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
        list_destroy(StreamIntList, &input);
    }

    it("aborts collection when the borrowed source mutates and preserves binding") {
        StreamIntList input = {0};
        StreamLongList output = {0};
        turbostl_stream_t pipeline = {0};
        turbostl_collect_result result;
        int seven = 7;

        check_true(stream_test_input(&input));
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.map(&pipeline, stream_square));
        check_equal(list_add(StreamIntList, &input, seven),
                    STL_CAPACITY_EXCEEDED);
        list_clear(StreamIntList, &input);
        result = collect(&pipeline, StreamLongList, &output, 6u);
        check_false(result.ok);
        check_equal(result.error, "range owner mutated");
        check_equal(result.status, CMETA_CALLBACK_ERROR);
        check_null(cmeta_container_descriptor(&output));
        check_equal(list_init(StreamLongList, &output, 6u), STL_OK);
        list_destroy(StreamLongList, &output);

        turbostl_stream_destroy(&pipeline);
        list_destroy(StreamIntList, &input);
    }

    it("aborts output when its collection limit is exceeded and preserves binding") {
        StreamIntList input = {0};
        StreamLongList output = {0};
        turbostl_stream_t pipeline = {0};
        turbostl_collect_result result;

        check_true(stream_test_input(&input));
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.filter(&pipeline, stream_keep_even)
                                   ->map(&pipeline, stream_square));
        result = collect(&pipeline, StreamLongList, &output, 2u);
        check_false(result.ok);
        check_equal(result.error, "observer rejected value");
        check_equal(result.status, CMETA_CAPACITY_EXCEEDED);
        check_null(cmeta_container_descriptor(&output));
        check_equal(list_init(StreamLongList, &output, 2u), STL_OK);
        list_destroy(StreamLongList, &output);

        turbostl_stream_destroy(&pipeline);
        list_destroy(StreamIntList, &input);
    }

    it("streams reflected students and computes their class average age") {
        static const StreamStudent students[] = {
            {101, 7, 18},
            {102, 7, 20},
            {103, 7, 22}
        };
        StreamAgeMap ages = {0};
        turbostl_stream_t pipeline = {0};
        cflow_result total = {0};
        size_t student_count;
        size_t index;

        check_equal(FieldCount(StreamStudent), (size_t)3u);
        check_not_null(FieldFind(StreamStudent, "age"));
        check_equal(map_init(StreamAgeMap, &ages, 3u), STL_OK);
        for (index = 0u; index < 3u; ++index) {
            int key = students[index].student_id;
            int value = students[index].age;
            check_equal(map_put(StreamAgeMap, &ages, key, value), STL_OK);
        }
        student_count = map_size(StreamAgeMap, &ages);
        check_not_null(stream_values(&ages, &pipeline));
        check_not_null(pipeline.map(&pipeline, stream_age_as_long)
                                   ->reduce(&pipeline, stream_sum_age));
        check_true(to_array(&pipeline, 1u, &total));
        check_equal(total.count, (size_t)1u);
        check_equal(*(const long *)total.data, 60L);
        check_equal((double)*(const long *)total.data / (double)student_count,
                    20.0);

        cflow_result_destroy(&total);
        turbostl_stream_destroy(&pipeline);
        map_destroy(StreamAgeMap, &ages);
    }
}
