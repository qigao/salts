#include <cmeta/struct.h>

Struct(StreamStudent,
    (int, student_id),
    (int, class_id),
    (int, age)
);

#include <turbostl/stream.h>

#include "tinytest.h"

#include <string.h>

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

typedef struct stream_visit_sum {
    int value;
    size_t calls;
} stream_visit_sum;

static bool stream_add_value(void *user,
                             const cmeta_type_desc *type,
                             const void *value) {
    stream_visit_sum *sum = (stream_visit_sum *)user;
    if (!sum || !value || !cmeta_type_equal(type, &cmeta_type_int))
        return false;
    sum->value += *(const int *)value;
    ++sum->calls;
    return true;
}

static bool stream_test_input(StreamIntList *input) {
    size_t index;

    if (StreamIntList_init(input, 6u) != STL_OK)
        return false;
    for (index = 1u; index <= 6u; ++index) {
        int value = (int)index;
        if (StreamIntList_push_back(input, value) != STL_OK) {
            StreamIntList_destroy(input);
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
        result = to_list_typed(&pipeline, StreamLongList, &output, 3u);
        check_equal(result.status, CMETA_OK);
        check_null(result.error);
        check_true(result.ok);
        check_equal(result.count, (size_t)3u);
        check_equal(StreamLongList_pop_front(&output, &value), STL_OK);
        check_equal(value, 4L);
        check_equal(StreamLongList_pop_front(&output, &value), STL_OK);
        check_equal(value, 16L);
        check_equal(StreamLongList_pop_front(&output, &value), STL_OK);
        check_equal(value, 36L);

        StreamLongList_destroy(&output);
        turbostl_stream_destroy(&pipeline);
        StreamIntList_destroy(&input);
    }

    it("retains PR #53 expression outputs through the erased terminal") {
        StreamIntList input = {0};
        list_t output = ListOf(long);
        turbostl_stream_t pipeline = {0};
        turbostl_collect_result result;

        check_true(stream_test_input(&input));
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.filter(&pipeline, stream_keep_even)
                                   ->map(&pipeline, stream_square));
        result = to_list(&pipeline, &output, 3u);
        check_true(result.ok);
        check_equal(list_size(&output), (size_t)3u);
        check_equal(*(const long *)list_front_const(&output), 4L);

        list_destroy(&output);
        turbostl_stream_destroy(&pipeline);
        StreamIntList_destroy(&input);
    }

    it("preserves erased expression binding after collection abort") {
        StreamIntList input = {0};
        list_t output = ListOf(long);
        turbostl_stream_t pipeline = {0};
        turbostl_collect_result result;

        check_true(stream_test_input(&input));
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.filter(&pipeline, stream_keep_even)
                                   ->map(&pipeline, stream_square));
        result = collect(&pipeline, &output, 2u);
        check_false(result.ok);
        check_equal(result.status, CMETA_CAPACITY_EXCEEDED);
        check_null(output.impl);
        check_true(output.element_type == CMETA_TYPEOF(long));
        check_equal(list_init(&output, 2u), STL_OK);

        list_destroy(&output);
        turbostl_stream_destroy(&pipeline);
        StreamIntList_destroy(&input);
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
        StreamIntList_destroy(&input);
    }

    it("exposes prefixed scalar and visiting terminal conveniences") {
        StreamIntList input = {0};
        turbostl_stream_t pipeline = {0};
        turbostl_find_result found = {0};
        stream_visit_sum sum = {0};
        const char *error = NULL;
        size_t count = 0u;
        bool matched = false;

        check_true(stream_test_input(&input));
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.filter(&pipeline, stream_keep_even)
                                   ->skip(&pipeline, 1u));
        check_true(turbostl_stream_count(&pipeline, &count, &error));
        check_equal(count, (size_t)2u);
        {
            turbostl_status_result result =
                turbostl_stream_count_result(&pipeline, &count);
            check_true(turbostl_status_result_is_ok(result));
            check_equal(result.status, CFLOW_STATUS_OK);
            check_true(strcmp(
                turbostl_status_result_message(result), "ok") == 0);
            check_equal(count, (size_t)2u);
        }
        check_true(turbostl_stream_any_match(
            &pipeline, stream_keep_even, &matched, &error));
        check_true(matched);
        check_true(turbostl_stream_all_match(
            &pipeline, stream_keep_even, &matched, &error));
        check_true(matched);
        check_true(turbostl_stream_find_first(
            &pipeline, &found, &error));
        check_true(turbostl_find_result_has_value(&found));
        check_true(cmeta_type_equal(
            turbostl_find_result_type(&found), &cmeta_type_int));
        check_equal(*(const int *)turbostl_find_result_value(&found), 4);
        check_true(turbostl_stream_for_each(
            &pipeline, stream_add_value, &sum, &error));
        check_equal(sum.calls, (size_t)2u);
        check_equal(sum.value, 10);
        check_null(error);

        turbostl_find_result_destroy(&found);
        turbostl_stream_destroy(&pipeline);
        StreamIntList_destroy(&input);
    }

    it("injects the bounded HashSet backend for stable distinct") {
        const int input_values[] = {3, 1, 3, 2, 1};
        const int expected[] = {3, 1, 2};
        StreamIntList input = {0};
        StreamIntList output = {0};
        turbostl_stream_t pipeline = {0};
        turbostl_collect_result collected;
        size_t index;

        check_equal(StreamIntList_init(&input, 5u), STL_OK);
        for (index = 0u; index < 5u; ++index)
            check_equal(
                StreamIntList_push_back(&input, input_values[index]), STL_OK);
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.distinct(&pipeline, 3u));
        collected = to_list_typed(
            &pipeline, StreamIntList, &output, 3u);
        check_true(collected.ok);
        check_equal(collected.count, (size_t)3u);
        for (index = 0u; index < 3u; ++index) {
            int value = 0;
            check_equal(StreamIntList_pop_front(&output, &value), STL_OK);
            check_equal(value, expected[index]);
        }

        StreamIntList_destroy(&output);
        turbostl_stream_destroy(&pipeline);
        StreamIntList_destroy(&input);
    }

    it("injects the bounded Vec backend for stable sorted") {
        const int input_values[] = {3, 1, 2, 1};
        const int expected[] = {1, 1, 2, 3};
        StreamIntList input = {0};
        StreamIntList output = {0};
        turbostl_stream_t pipeline = {0};
        turbostl_collect_result collected;
        size_t index;

        check_equal(StreamIntList_init(&input, 4u), STL_OK);
        for (index = 0u; index < 4u; ++index)
            check_equal(
                StreamIntList_push_back(&input, input_values[index]), STL_OK);
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.sorted(&pipeline, 4u));
        collected = to_list_typed(
            &pipeline, StreamIntList, &output, 4u);
        check_true(collected.ok);
        check_equal(collected.count, (size_t)4u);
        for (index = 0u; index < 4u; ++index) {
            int value = 0;
            check_equal(StreamIntList_pop_front(&output, &value), STL_OK);
            check_equal(value, expected[index]);
        }

        StreamIntList_destroy(&output);
        turbostl_stream_destroy(&pipeline);
        StreamIntList_destroy(&input);
    }

    it("reports the TurboSTL sorted hard bound structurally") {
        const int input_values[] = {3, 1, 2};
        StreamIntList input = {0};
        turbostl_stream_t pipeline = {0};
        turbostl_status_result result;
        size_t count = 99u;
        size_t index;

        check_equal(StreamIntList_init(&input, 3u), STL_OK);
        for (index = 0u; index < 3u; ++index)
            check_equal(
                StreamIntList_push_back(&input, input_values[index]), STL_OK);
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.sorted(&pipeline, 2u));
        result = turbostl_stream_count_result(&pipeline, &count);
        check_equal(result.status, CFLOW_STATUS_CAPACITY_EXCEEDED);
        check_equal(count, (size_t)0u);

        turbostl_stream_destroy(&pipeline);
        StreamIntList_destroy(&input);
    }

    it("preserves runtime and collector status when sorted collection fails") {
        const int input_values[] = {3, 1, 2};
        StreamIntList input = {0};
        StreamIntList output = {0};
        turbostl_stream_t pipeline = {0};
        turbostl_collect_result result;
        size_t index;

        check_equal(StreamIntList_init(&input, 3u), STL_OK);
        for (index = 0u; index < 3u; ++index)
            check_equal(
                StreamIntList_push_back(&input, input_values[index]), STL_OK);
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.sorted(&pipeline, 2u));
        result = collect_typed(&pipeline, StreamIntList, &output, 3u);
        check_false(result.ok);
        check_equal(result.flow_status, CFLOW_STATUS_CAPACITY_EXCEEDED);
        check_equal(result.status, CMETA_CALLBACK_ERROR);
        check_equal(result.count, (size_t)0u);
        check_null(cmeta_container_descriptor(&output));

        turbostl_stream_destroy(&pipeline);
        StreamIntList_destroy(&input);
    }

    it("reports the TurboSTL distinct hard bound structurally") {
        const int input_values[] = {1, 2, 3};
        StreamIntList input = {0};
        turbostl_stream_t pipeline = {0};
        turbostl_status_result result;
        size_t count = 99u;
        size_t index;

        check_equal(StreamIntList_init(&input, 3u), STL_OK);
        for (index = 0u; index < 3u; ++index)
            check_equal(
                StreamIntList_push_back(&input, input_values[index]), STL_OK);
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.distinct(&pipeline, 2u));
        result = turbostl_stream_count_result(&pipeline, &count);
        check_equal(result.status, CFLOW_STATUS_CAPACITY_EXCEEDED);
        check_equal(count, (size_t)0u);

        turbostl_stream_destroy(&pipeline);
        StreamIntList_destroy(&input);
    }

    it("rejects an array result that exceeds its explicit limit") {
        StreamIntList input = {0};
        turbostl_stream_t pipeline = {0};
        cflow_result output = {0};
        turbostl_status_result result;

        check_true(stream_test_input(&input));
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.filter(&pipeline, stream_keep_even)
                                   ->map(&pipeline, stream_square));
        result = to_array_result(&pipeline, 2u, &output);
        check_equal(result.status, CFLOW_STATUS_CAPACITY_EXCEEDED);
        check_null(output.data);
        check_equal(output.count, (size_t)0u);
        check_null(output.type);

        turbostl_stream_destroy(&pipeline);
        StreamIntList_destroy(&input);
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
        check_equal(StreamIntList_push_back(&input, seven),
                    STL_CAPACITY_EXCEEDED);
        StreamIntList_clear(&input);
        result = collect_typed(&pipeline, StreamLongList, &output, 6u);
        check_false(result.ok);
        check_equal(result.flow_status, CFLOW_STATUS_EXECUTION_ERROR);
        check_equal(result.error, "range owner mutated");
        check_equal(result.status, CMETA_CALLBACK_ERROR);
        check_null(cmeta_container_descriptor(&output));
        check_equal(StreamLongList_init(&output, 6u), STL_OK);
        StreamLongList_destroy(&output);

        turbostl_stream_destroy(&pipeline);
        StreamIntList_destroy(&input);
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
        result = collect_typed(&pipeline, StreamLongList, &output, 2u);
        check_false(result.ok);
        check_equal(result.flow_status, CFLOW_STATUS_CAPACITY_EXCEEDED);
        check_equal(result.error, "observer rejected value");
        check_equal(result.status, CMETA_CAPACITY_EXCEEDED);
        check_null(cmeta_container_descriptor(&output));
        check_equal(StreamLongList_init(&output, 2u), STL_OK);
        StreamLongList_destroy(&output);

        turbostl_stream_destroy(&pipeline);
        StreamIntList_destroy(&input);
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
        check_equal(StreamAgeMap_init(&ages, 3u), STL_OK);
        for (index = 0u; index < 3u; ++index) {
            int key = students[index].student_id;
            int value = students[index].age;
            check_equal(StreamAgeMap_put(&ages, key, value), STL_OK);
        }
        student_count = StreamAgeMap_size(&ages);
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
        StreamAgeMap_destroy(&ages);
    }
}
