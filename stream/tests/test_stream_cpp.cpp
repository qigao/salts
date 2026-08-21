#include "stream_container.h"
#include "stream_live.h"
#include "stream_turbo_containers.h"
#include "stream.hpp"
#include "tinytest.hpp"

#include <cstdint>
#include <type_traits>

TURBO_VEC_DEFINE(cpp_int_vec_t, int)
TURBO_HASH_MAP_DEFINE(cpp_int_map_t, int, int)

static_assert(std::is_standard_layout<stream_t>::value, "stream_t must remain standard layout");

static stream_result_t cpp_double_int(const void *input, void *output)
{
    *static_cast<int *>(output) = *static_cast<const int *>(input) * 2;
    return STREAM_OK;
}

static size_t cpp_peek_count = 0;
static int cpp_peek_sum = 0;
static void cpp_sum_peek_consumer(const void *value)
{
    ++cpp_peek_count;
    cpp_peek_sum += *static_cast<const int *>(value);
}

static bool cpp_is_even(const void *value)
{
    return (*static_cast<const int *>(value) & 1) == 0;
}

static stream_result_t cpp_sum_reducer(void *accumulator, const void *value)
{
    *static_cast<int *>(accumulator) += *static_cast<const int *>(value);
    return STREAM_OK;
}

static bool cpp_int_equal(const void *a, const void *b)
{
    return *static_cast<const int *>(a) == *static_cast<const int *>(b);
}

spec("C++ stream consumer") {
    it("includes the public headers and reads a persistent array view") {
        const int values[] = {4, 5};
        const stream_array_view_t view = stream_array_view(values, 2, sizeof(values[0]));
        stream_t stream{};
        stream_item_t item{};

        check_equal(stream_from_array_view(&stream, &view), STREAM_OK);
        check_equal(stream_next_view(&stream, &item), STREAM_OK);
        check_equal(*static_cast<const int *>(item.data), 4);
    }

    it("adapts a TurboUtils vector from C++") {
        turbo_vec_t vec{};
        stream_t stream{};
        stream_item_t item{};
        int value = 9;

        check_equal(turbo_vec_init(&vec, sizeof(int)), TURBO_OK);
        check_equal(turbo_vec_push(&vec, &value), TURBO_OK);
        check_equal(stream_from_turbo_vec(&stream, &vec), STREAM_OK);
        check_equal(stream_next_view(&stream, &item), STREAM_OK);
        check_equal(*static_cast<const int *>(item.data), 9);
        turbo_vec_destroy(&vec);
    }

    it("uses typed bulk constructors from C++") {
        const int values[] = {2, 4, 6};
        const cpp_int_map_t_entry entries[] = {{1, 10}, {2, 20}};
        cpp_int_vec_t vec{};
        cpp_int_map_t map{};

        check_equal(cpp_int_vec_t_from(&vec, values, 3), TURBO_OK);
        check_equal(cpp_int_map_t_from(&map, entries, 2), TURBO_OK);
        check_equal(*cpp_int_vec_t_at_const(&vec, 2), 6);
        check_equal(*cpp_int_map_t_get_const(&map, 2), 20);

        cpp_int_map_t_destroy(&map);
        cpp_int_vec_t_destroy(&vec);
    }

    it("creates a stream from a C++ initializer list") {
        stream_t stream{};
        stream_item_t item{};
        int output = 0;

        check_equal(STREAM_OF(&stream, int, 5, 6, 7), STREAM_OK);
        item.data = &output;
        item.size = sizeof(output);
        check_equal(stream_next(&stream, &item), STREAM_OK);
        check_equal(output, 5);
    }

    it("creates a bounded iterate source from C++") {
        stream_t stream{};
        int output[4]{};
        size_t count = 0;

        check_equal(
            STREAM_ITERATE(&stream, int, 2, 3, cpp_double_int),
            STREAM_OK);
        check_equal(STREAM_TO_ARRAY(&stream, output, &count), STREAM_END);
        check_equal(count, 3);
        check_equal(output[0], 2);
        check_equal(output[1], 4);
        check_equal(output[2], 8);
    }

    it("iterates with stream.hpp range view") {
        stream_t stream{};
        int values[3];
        size_t value_count = 0;
        int sum = 0;

        check_equal(STREAM_OF(&stream, int, 5, 6, 7), STREAM_OK);
        for (int value : turbo::stream::from<int>(stream)) {
            values[value_count++] = value;
            sum += value;
        }

        check_equal(value_count, 3);
        check_equal(values[0], 5);
        check_equal(values[1], 6);
        check_equal(values[2], 7);
        check_equal(sum, 18);
    }

    it("iterates stream.hpp range view from pointer and validates size-mismatch") {
        stream_t stream{};
        int count = 0;

        check_equal(STREAM_OF(&stream, int, 1, 2), STREAM_OK);
        for (int64_t value_from_ptr : turbo::stream::from<int64_t>(&stream)) {
            (void)value_from_ptr;
            ++count;
        }

        check_equal(stream.error, STREAM_ERR_BAD_ARGUMENT);
        check_equal(count, 0);
    }

    it("iterates a C++ snapshot view") {
        stream_t source{};
        stream_t snapshot{};
        int output = 0;
        size_t count = 0;
        int sum = 0;

        check_equal(STREAM_OF(&source, int, 1, 2, 3, 4), STREAM_OK);
        stream_item_t item{
            &output,
            sizeof(output),
            0,
            0};
        check_equal(stream_next(&source, &item), STREAM_OK);
        check_equal(output, 1);

        for (int value : turbo::stream::from_snapshot<int>(source, snapshot)) {
            sum += value;
            ++count;
        }

        check_equal(count, 3);
        check_equal(sum, 9);
        check_equal(snapshot.error, STREAM_ERR_NONE);
    }

    it("creates a snapshot view through C++ view chaining") {
        stream_t source{};
        stream_t snapshot{};
        int output = 0;
        size_t count = 0;
        int sum = 0;

        check_equal(STREAM_OF(&source, int, 1, 2, 3, 4, 5), STREAM_OK);

        stream_item_t item{
            &output,
            sizeof(output),
            0,
            0};
        check_equal(stream_next(&source, &item), STREAM_OK);
        check_equal(output, 1);

        auto snapshot_view = turbo::stream::from<int>(source).snapshot(snapshot).filter(cpp_is_even);
        for (int value : snapshot_view) {
            sum += value;
            ++count;
        }

        check_equal(count, 2);
        check_equal(sum, 6);
        check_equal(snapshot.error, STREAM_ERR_NONE);

        check_equal(stream_next(&source, &item), STREAM_OK);
        check_equal(output, 2);
    }

    it("returns empty snapshot view when source cannot be cloned") {
        stream_t source{};
        stream_t snapshot{};
        int storage = 0;
        uint64_t timestamp = 0;
        uint64_t sequence = 0;
        stream_live_ring_t ring;
        int value_count = 0;

        check_equal(
            stream_live_ring_init(
                &ring, &storage, &timestamp, &sequence, 4, sizeof(storage),
                STREAM_BP_REJECT_NEW),
            STREAM_OK);
        check_equal(stream_from_live_ring(&source, &ring), STREAM_OK);

        auto snapshot_view = turbo::stream::from<int>(source).snapshot(snapshot);
        for (const auto &value : snapshot_view.filter(cpp_is_even)) {
            (void)value;
            ++value_count;
        }

        check_equal(value_count, 0);
        check_equal(snapshot.error, STREAM_ERR_UNSUPPORTED_SOURCE);
    }

    it("iterates boxed stream values as pointer references") {
        stream_t stream{};
        int sum = 0;
        int items = 0;

        check_equal(STREAM_OF(&stream, int, 3, 4, 5), STREAM_OK);
        check_equal(stream.boxed(&stream), &stream);

        for (const int *value : turbo::stream::from_boxed<int>(stream)) {
            if (value != nullptr) {
                sum += *value;
                ++items;
            }
        }

        check_equal(items, 3);
        check_equal(sum, 12);
    }

    it("chains skip/take/peek with a filter in stream.hpp view") {
        stream_t stream{};
        cpp_peek_count = 0;
        cpp_peek_sum = 0;
        size_t count = 0;
        int sum = 0;

        check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4, 5, 6), STREAM_OK);
        for (int value : turbo::stream::from<int>(stream)
                           .filter(cpp_is_even)
                           .skip(1)
                           .take(2)
                           .peek(cpp_sum_peek_consumer)) {
            sum += value;
            ++count;
        }

        check_equal(count, 2);
        check_equal(sum, 10);
        check_equal(cpp_peek_count, 2);
        check_equal(cpp_peek_sum, 10);
    }

    it("maps and then chains skip/take in stream.hpp") {
        stream_t stream{};
        size_t count = 0;
        int sum = 0;

        check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4), STREAM_OK);
        for (int value : turbo::stream::from<int>(stream).map<int>(sizeof(int), cpp_double_int)
                           .skip(1)
                           .take(2)) {
            sum += value;
            ++count;
        }

        check_equal(count, 2);
        check_equal(sum, 10);
    }

    it("supports stream.hpp view and boxed_view for_each") {
        stream_t stream{};
        int sum = 0;
        size_t count = 0;

        check_equal(STREAM_OF(&stream, int, 7, 8, 9), STREAM_OK);
        turbo::stream::from<int>(stream).for_each([&](int value) {
            sum += value;
            ++count;
        });
        check_equal(count, 3);
        check_equal(sum, 24);

        stream_t boxed_stream{};
        check_equal(STREAM_OF(&boxed_stream, int, 3, 4, 5), STREAM_OK);
        int boxed_sum = 0;
        size_t boxed_count = 0;
        turbo::stream::from<int>(boxed_stream).boxed().for_each(
            [&](const int *value) {
                if (value) {
                    boxed_sum += *value;
                    ++boxed_count;
                }
            });
        check_equal(boxed_count, 3);
        check_equal(boxed_sum, 12);
    }

    it("exposes stream terminal wrappers in stream.hpp view") {
        stream_t stream{};
        size_t count = 0;
        int sum = 0;
        bool matched = false;
        bool contains = false;
        int first = 0;
        int out[4] = {0};
        size_t array_count = 0;

        check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4), STREAM_OK);
        check_equal(turbo::stream::from<int>(stream).count(count), STREAM_END);
        check_equal(count, 4);

        check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4), STREAM_OK);
        sum = 0;
        check_equal(turbo::stream::from<int>(stream).reduce(sum, cpp_sum_reducer),
                    STREAM_END);
        check_equal(sum, 10);

        check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4), STREAM_OK);
        check_equal(turbo::stream::from<int>(stream).find_first(first), STREAM_OK);
        check_equal(first, 1);

        check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4), STREAM_OK);
        matched = false;
        check_equal(turbo::stream::from<int>(stream).any_match(cpp_is_even, matched),
                    STREAM_OK);
        check_equal(matched, true);

        check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4), STREAM_OK);
        contains = false;
        check_equal(turbo::stream::from<int>(stream).contains(3, cpp_int_equal, contains),
                    STREAM_OK);
        check_equal(contains, true);

        check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4), STREAM_OK);
        auto to_array_result = turbo::stream::from<int>(stream).to_array(out, 4, array_count);
        check_equal(array_count, 4);
        check_equal(to_array_result, STREAM_FULL);
        static const int expected[] = {1, 2, 3, 4};
        check_equal(out, expected, sizeof(expected));
        check_equal(out[0], 1);
        check_equal(out[1], 2);
        check_equal(out[2], 3);
        check_equal(out[3], 4);
    }
}
