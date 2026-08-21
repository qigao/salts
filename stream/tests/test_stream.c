#include "stream.h"
#include "stream_container.h"
#include "stream_live.h"
#include "stream_spsc.h"
#include "tinytest.h"

#include <limits.h>
#include <stdint.h>

typedef struct {
    int close_count;
} close_source_state_t;

static stream_result_t failing_source_next(stream_source_t *source, stream_item_t *out)
{
    (void)source;
    (void)out;
    return STREAM_ERROR;
}

static stream_result_t failing_source_reset(stream_source_t *source)
{
    (void)source;
    return STREAM_ERROR;
}

static stream_result_t empty_source_next(stream_source_t *source, stream_item_t *out)
{
    (void)source;
    (void)out;
    return STREAM_END;
}

static void count_source_close(stream_source_t *source)
{
    close_source_state_t *state = (close_source_state_t *)source->context;
    ++state->close_count;
}

static bool int_equal(const void *left, const void *right)
{
    return *(const int *)left == *(const int *)right;
}

static bool int_is_even(const void *value)
{
    return *(const int *)value % 2 == 0;
}

static stream_result_t int_times_ten(const void *input, void *output)
{
    *(int *)output = *(const int *)input * 10;
    return STREAM_OK;
}

static int peek_total;

static void add_to_peek_total(const void *value)
{
    peek_total += *(const int *)value;
}

static stream_result_t add_int(void *accumulator, const void *value)
{
    *(int *)accumulator += *(const int *)value;
    return STREAM_OK;
}

static stream_result_t fail_reduce(void *accumulator, const void *value)
{
    (void)accumulator;
    (void)value;
    return STREAM_ERROR;
}

static bool int_greater_than_three(const void *value)
{
    return *(const int *)value > 3;
}

static int int_compare(const void *left, const void *right)
{
    const int a = *(const int *)left;
    const int b = *(const int *)right;
    return (a > b) - (a < b);
}

static bool int_less_than_four(const void *value)
{
    return *(const int *)value < 4;
}

static int drop_while_predicate_calls;

static bool tracked_int_less_than_three(const void *value)
{
    ++drop_while_predicate_calls;
    return *(const int *)value < 3;
}

typedef struct {
    int key;
    int order;
} sort_entry_t;

typedef struct {
    int sum;
    size_t count;
} int_summary_t;

static int sort_entry_compare(const void *left, const void *right)
{
    const sort_entry_t *a = (const sort_entry_t *)left;
    const sort_entry_t *b = (const sort_entry_t *)right;
    return (a->key > b->key) - (a->key < b->key);
}

static stream_result_t emit_value_and_negative(
    const void *input,
    stream_emitter_t *emitter)
{
    const int value = *(const int *)input;
    const int negative = -value;
    stream_result_t r = emitter->emit(emitter, &value);

    if (r != STREAM_OK) {
        return r;
    }
    return emitter->emit(emitter, &negative);
}

static stream_result_t double_int(const void *input, void *output)
{
    *(int *)output = *(const int *)input * 2;
    return STREAM_OK;
}

static stream_result_t generate_square(size_t index, void *output)
{
    *(int *)output = (int)(index * index);
    return STREAM_OK;
}

static stream_result_t summarize_int(void *result, const void *value)
{
    int_summary_t *summary = (int_summary_t *)result;

    summary->sum += *(const int *)value;
    ++summary->count;
    return STREAM_OK;
}

static stream_result_t fail_collect(void *result, const void *value)
{
    (void)result;
    (void)value;
    return STREAM_ERROR;
}

spec("stream") {
    group("values factory") {
        it("creates a fluent stream directly from typed values") {
            stream_t stream;
            stream_item_t item;
            int output = 0;

            check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4), STREAM_OK);
            stream.filter(&stream, int_is_even)->take(&stream, 2);

            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream.next(&stream, &item), STREAM_OK);
            check_equal(output, 2);
            item.size = sizeof(output);
            check_equal(stream.next(&stream, &item), STREAM_OK);
            check_equal(output, 4);
            item.size = sizeof(output);
            check_equal(stream.next(&stream, &item), STREAM_END);
        }

        it("creates an independent snapshot after partial traversal") {
            stream_t source;
            stream_t snapshot;
            stream_item_t item;
            int output = 0;

            check_equal(STREAM_OF(&source, int, 1, 2, 3, 4), STREAM_OK);

            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream_next(&source, &item), STREAM_OK);
            check_equal(output, 1);

            check_equal(stream_snapshot_init(&snapshot, &source), STREAM_OK);
            check_equal(snapshot.error, STREAM_ERR_NONE);

            check_equal(stream_next(&source, &item), STREAM_OK);
            check_equal(output, 2);
            check_equal(stream_next(&source, &item), STREAM_OK);
            check_equal(output, 3);

            check_equal(stream_next(&snapshot, &item), STREAM_OK);
            check_equal(output, 2);
            check_equal(stream_next(&snapshot, &item), STREAM_OK);
            check_equal(output, 3);
            check_equal(stream_next(&snapshot, &item), STREAM_OK);
            check_equal(output, 4);

            check_equal(stream_next(&source, &item), STREAM_OK);
            check_equal(output, 4);
            check_equal(stream_next(&source, &item), STREAM_END);
            check_equal(stream_next(&snapshot, &item), STREAM_END);
        }

        it("owns copies of direct values") {
            int values[] = {7, 8};
            stream_t stream;
            stream_item_t item;
            int output = 0;

            check_equal(stream_from_values(&stream, values, 2, sizeof(values[0])), STREAM_OK);
            values[0] = 99;

            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output, 7);
        }

        it("preserves owned values when clearing operator state") {
            const int expected[] = {1, 2};
            stream_t stream;
            stream_item_t item;
            stream_window_t window;

            check_equal(STREAM_OF(&stream, int, 1, 2, 3), STREAM_OK);
            stream_window(&stream, 2);
            stream_clear(&stream);
            check_equal(stream_reset(&stream), STREAM_OK);
            stream_window(&stream, 2);

            item.data = &window;
            item.size = sizeof(window);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(window.data, expected, sizeof(expected));
        }

        it("rejects values that exceed owned state capacity") {
            unsigned char value = 1;
            stream_t stream;

            check_equal(stream_from_values(
                             &stream, &value, STREAM_MAX_STATE_SIZE + 1U, sizeof(value)),
                         STREAM_ERROR);
            check_equal(stream.error, STREAM_ERR_STATE_FULL);
        }

        it("accepts values that exactly fill owned state capacity") {
            unsigned char values[STREAM_MAX_STATE_SIZE] = {42};
            stream_t stream;
            stream_item_t item;
            unsigned char output = 0;

            check_equal(stream_from_values(
                             &stream, values, STREAM_MAX_STATE_SIZE, sizeof(values[0])),
                         STREAM_OK);
            stream_clear(&stream);
            check_equal(stream_reset(&stream), STREAM_OK);
            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output, 42);
        }

        it("creates an empty typed stream") {
            stream_t stream;
            stream_item_t item;
            int output = 0;

            check_equal(STREAM_EMPTY(&stream, int), STREAM_OK);
            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_END);
        }
    }

    group("range source") {
        it("creates an ascending exclusive range") {
            const int64_t expected[] = {-2, -1, 0, 1, 2};
            int64_t output[6] = {0};
            stream_t stream;
            size_t count = 0;

            check_equal(STREAM_RANGE(&stream, -2, 3), STREAM_OK);
            check_equal(STREAM_TO_ARRAY(&stream, output, &count), STREAM_END);
            check_equal(count, 5);
            check_equal(output, expected, sizeof(expected));
        }

        it("supports descending ranges with a negative step") {
            const int64_t expected[] = {5, 3, 1};
            int64_t output[4] = {0};
            stream_t stream;
            size_t count = 0;

            check_equal(STREAM_RANGE_STEP(&stream, 5, -1, -2), STREAM_OK);
            check_equal(STREAM_TO_ARRAY(&stream, output, &count), STREAM_END);
            check_equal(count, 3);
            check_equal(output, expected, sizeof(expected));
        }

        it("resets a range to its initial value and sequence") {
            stream_t stream;
            stream_item_t item;
            int64_t output = 0;

            check_equal(STREAM_RANGE(&stream, 4, 7), STREAM_OK);
            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream.next(&stream, &item), STREAM_OK);
            check_equal(output, 4);
            check_equal(stream.reset(&stream), STREAM_OK);
            item.size = sizeof(output);
            check_equal(stream.next(&stream, &item), STREAM_OK);
            check_equal(output, 4);
            check_equal(item.sequence, 0);
        }

        it("does not overflow near the signed range boundary") {
            int64_t output[2] = {0};
            stream_t stream;
            size_t count = 0;

            check_equal(
                STREAM_RANGE_STEP(&stream, INT64_MAX - 1, INT64_MAX, INT64_MAX),
                STREAM_OK);
            check_equal(STREAM_TO_ARRAY(&stream, output, &count), STREAM_END);
            check_equal(count, 1);
            check_true(output[0] == INT64_MAX - 1);
        }

        it("rejects a zero range step") {
            stream_t stream;

            check_equal(STREAM_RANGE_STEP(&stream, 0, 3, 0), STREAM_ERROR);
            check_equal(stream.error, STREAM_ERR_BAD_ARGUMENT);
        }
    }

    group("generated sources") {
        it("iterates from an owned seed up to a fixed limit") {
            const int expected[] = {1, 2, 4, 8};
            int output[5] = {0};
            stream_t stream;
            size_t count = 0;

            check_equal(STREAM_ITERATE(&stream, int, 1, 4, double_int), STREAM_OK);
            check_equal(STREAM_TO_ARRAY(&stream, output, &count), STREAM_END);
            check_equal(count, 4);
            check_equal(output, expected, sizeof(expected));

            check_equal(stream.reset(&stream), STREAM_OK);
            count = 0;
            check_equal(STREAM_TO_ARRAY(&stream, output, &count), STREAM_END);
            check_equal(count, 4);
            check_equal(output, expected, sizeof(expected));
        }

        it("generates values from resettable indices") {
            const int expected[] = {0, 1, 4, 9};
            int output[5] = {0};
            stream_t stream;
            size_t count = 0;

            check_equal(
                STREAM_GENERATE(&stream, int, 4, generate_square),
                STREAM_OK);
            check_equal(STREAM_TO_ARRAY(&stream, output, &count), STREAM_END);
            check_equal(count, 4);
            check_equal(output, expected, sizeof(expected));
        }
    }

    group("materializing operations") {
        it("sorts stably and remains resettable") {
            const sort_entry_t values[] = {
                {2, 0}, {1, 1}, {2, 2}, {1, 3}, {3, 4}
            };
            sort_entry_t output[6] = {{0, 0}};
            stream_t stream;
            size_t count = 0;

            check_equal(
                stream_from_values(&stream, values, 5, sizeof(values[0])),
                STREAM_OK);
            stream.sorted(&stream, 5, sort_entry_compare);
            check_equal(stream.error, STREAM_ERR_NONE);
            check_equal(STREAM_TO_ARRAY(&stream, output, &count), STREAM_END);
            check_equal(count, 5);
            check_equal(output[0].key, 1);
            check_equal(output[0].order, 1);
            check_equal(output[1].key, 1);
            check_equal(output[1].order, 3);
            check_equal(output[2].key, 2);
            check_equal(output[2].order, 0);
            check_equal(output[3].key, 2);
            check_equal(output[3].order, 2);
            check_equal(output[4].key, 3);

            check_equal(stream.reset(&stream), STREAM_OK);
            count = 0;
            check_equal(STREAM_TO_ARRAY(&stream, output, &count), STREAM_END);
            check_equal(count, 5);
            check_equal(output[0].order, 1);
        }

        it("fails explicitly when sorted exceeds its item limit") {
            int values[] = {3, 2, 1};
            stream_array_source_state_t source_state;
            stream_t stream;

            check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
            stream.sorted(&stream, 2, int_compare);
            check_equal(stream.error, STREAM_ERR_SORT_FULL);
            check_equal(source_state.pos, 3);
        }

        it("flat maps each input into copied output values") {
            const int expected[] = {1, -1, 2, -2, 3, -3};
            int output[7] = {0};
            stream_t stream;
            size_t count = 0;

            check_equal(STREAM_OF(&stream, int, 1, 2, 3), STREAM_OK);
            stream.flat_map(
                &stream, sizeof(int), 6, emit_value_and_negative);
            check_equal(stream.error, STREAM_ERR_NONE);
            check_equal(STREAM_TO_ARRAY(&stream, output, &count), STREAM_END);
            check_equal(count, 6);
            check_equal(output, expected, sizeof(expected));
        }

        it("fails explicitly when flat_map exceeds its output limit") {
            stream_t stream;

            check_equal(STREAM_OF(&stream, int, 1, 2), STREAM_OK);
            stream.flat_map(
                &stream, sizeof(int), 2, emit_value_and_negative);
            check_equal(stream.error, STREAM_ERR_FLAT_MAP_FULL);
            check_equal(
                stream_error_string(stream.error),
                "flat_map output limit reached");
        }

        it("rejects materializing a temporarily empty live source") {
            int storage = 0;
            uint64_t timestamp = 0;
            uint64_t sequence = 0;
            stream_live_ring_t ring;
            stream_t stream;

            check_equal(stream_live_ring_init(
                             &ring, &storage, &timestamp, &sequence, 1,
                             sizeof(storage), STREAM_BP_REJECT_NEW),
                         STREAM_OK);
            check_equal(stream_from_live_ring(&stream, &ring), STREAM_OK);
            stream.sorted(&stream, 1, int_compare);
            check_equal(stream.error, STREAM_ERR_NEEDS_FINITE_SOURCE);
            check_equal(
                stream_error_string(stream.error),
                "operation requires a finite source");
        }

        it("concatenates two finite streams in source order") {
            const int expected[] = {1, 2, 3, 4};
            int left_values[] = {1, 2};
            int right_values[] = {3, 4};
            int output[5] = {0};
            stream_array_source_state_t left_state;
            stream_array_source_state_t right_state;
            stream_t left;
            stream_t right;
            size_t count = 0;

            check_equal(
                STREAM_ARRAY_INIT(&left, &left_state, left_values),
                STREAM_OK);
            check_equal(
                STREAM_ARRAY_INIT(&right, &right_state, right_values),
                STREAM_OK);
            left.concat(&left, &right, 5);
            check_equal(left.error, STREAM_ERR_NONE);
            check_equal(STREAM_TO_ARRAY(&left, output, &count), STREAM_END);
            check_equal(count, 4);
            check_equal(output, expected, sizeof(expected));

            check_equal(left.reset(&left), STREAM_OK);
            count = 0;
            check_equal(STREAM_TO_ARRAY(&left, output, &count), STREAM_END);
            check_equal(count, 4);
            check_equal(output, expected, sizeof(expected));
        }

        it("rejects concat element-size mismatches") {
            stream_t left;
            stream_t right;

            check_equal(STREAM_OF(&left, int, 1), STREAM_OK);
            check_equal(STREAM_OF(&right, int64_t, 2), STREAM_OK);
            left.concat(&left, &right, 2);
            check_equal(left.error, STREAM_ERR_BAD_ARGUMENT);
        }

        it("reports concat capacity without pulling another value") {
            int left_values[] = {1, 2};
            int right_values[] = {3, 4};
            stream_array_source_state_t left_state;
            stream_array_source_state_t right_state;
            stream_t left;
            stream_t right;

            check_equal(
                STREAM_ARRAY_INIT(&left, &left_state, left_values),
                STREAM_OK);
            check_equal(
                STREAM_ARRAY_INIT(&right, &right_state, right_values),
                STREAM_OK);
            left.concat(&left, &right, 3);
            check_equal(left.error, STREAM_ERR_CONCAT_FULL);
            check_equal(right_state.pos, 1);
        }
    }

    group("Java-style operations") {
        it("peeks at values in declaration order") {
            stream_t stream;
            size_t count = 0;

            peek_total = 0;
            check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4), STREAM_OK);
            check_equal(
                stream.filter(&stream, int_is_even)
                      ->peek(&stream, add_to_peek_total)
                      ->count(&stream, &count),
                STREAM_END);
            check_equal(count, 2);
            check_equal(peek_total, 6);
        }

        it("boxes each element into a pointer") {
            stream_t stream;
            stream_item_t item;
            const void *boxed = NULL;

            check_equal(STREAM_OF(&stream, int, 10, 20, 30), STREAM_OK);
            stream.boxed(&stream);
            item.data = &boxed;
            item.size = sizeof(boxed);
            check_equal(stream.next(&stream, &item), STREAM_OK);
            check_equal(*(const int *)boxed, 10);
            check_equal(stream.next(&stream, &item), STREAM_OK);
            check_equal(*(const int *)boxed, 20);
            check_equal(stream.next(&stream, &item), STREAM_OK);
            check_equal(*(const int *)boxed, 30);
            check_equal(stream.next(&stream, &item), STREAM_END);
        }

        it("supports fluent skip and boxed composition") {
            stream_t stream;
            stream_item_t item;
            const void *boxed = NULL;

            check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4), STREAM_OK);
            item.data = &boxed;
            item.size = sizeof(boxed);
            check_equal(
                stream.skip(&stream, 2)->boxed(&stream)->next(&stream, &item),
                STREAM_OK);
            check_equal(*(const int *)boxed, 3);
            check_equal(stream.next(&stream, &item), STREAM_OK);
            check_equal(*(const int *)boxed, 4);
            check_equal(stream.next(&stream, &item), STREAM_END);
        }

        it("counts values after intermediate operations") {
            stream_t stream;
            size_t count = 0;

            check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4, 5), STREAM_OK);
            check_equal(
                stream.filter(&stream, int_is_even)->count(&stream, &count),
                STREAM_END);
            check_equal(count, 2);
        }

        it("reduces values into a caller-owned accumulator") {
            stream_t stream;
            int sum = 10;

            check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4), STREAM_OK);
            check_equal(stream.reduce(&stream, &sum, add_int), STREAM_END);
            check_equal(sum, 20);
        }

        it("records reduce callback failures") {
            stream_t stream;
            int sum = 0;

            check_equal(STREAM_OF(&stream, int, 1), STREAM_OK);
            check_equal(stream.reduce(&stream, &sum, fail_reduce), STREAM_ERROR);
            check_equal(stream.error, STREAM_ERR_REDUCE_FAILED);
            check_equal(stream_error_string(stream.error), "reduce callback failed");
        }

        it("finds the first pipeline value without consuming the next one") {
            int values[] = {1, 2, 4, 6};
            stream_array_source_state_t source_state;
            stream_t stream;
            stream_item_t item;
            int output = 0;

            check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
            stream_filter(&stream, int_is_even);
            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream.find_first(&stream, &item), STREAM_OK);
            check_equal(output, 2);
            check_equal(source_state.pos, 2);
        }

        it("short-circuits any_match at the first match") {
            int values[] = {1, 2, 4, 6};
            stream_array_source_state_t source_state;
            stream_t stream;
            bool matched = false;

            check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
            check_equal(
                stream.any_match(&stream, int_greater_than_three, &matched),
                STREAM_OK);
            check_true(matched);
            check_equal(source_state.pos, 3);
        }

        it("short-circuits all_match at the first mismatch") {
            int values[] = {2, 4, 5, 6};
            stream_array_source_state_t source_state;
            stream_t stream;
            bool matched = true;

            check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
            check_equal(stream.all_match(&stream, int_is_even, &matched), STREAM_OK);
            check_false(matched);
            check_equal(source_state.pos, 3);
        }

        it("short-circuits none_match at the first match") {
            int values[] = {1, 3, 4, 5};
            stream_array_source_state_t source_state;
            stream_t stream;
            bool matched = true;

            check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
            check_equal(stream.none_match(&stream, int_is_even, &matched), STREAM_OK);
            check_false(matched);
            check_equal(source_state.pos, 3);
        }

        it("short-circuits contains at the first matching value") {
            int values[] = {1, 4, 6};
            stream_array_source_state_t source_state;
            stream_t stream;
            bool found = false;

            check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
            check_equal(
                stream.contains(&stream, &(int){6}, int_equal, &found),
                STREAM_OK);
            check_true(found);
            check_equal(source_state.pos, 3);
        }

        it("returns END when target is absent and consumes all values") {
            int values[] = {1, 2, 3};
            stream_array_source_state_t source_state;
            stream_t stream;
            bool found = true;

            check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
            check_equal(
                stream.contains(&stream, &(int){99}, int_equal, &found),
                STREAM_END);
            check_false(found);
            check_equal(source_state.pos, 3);
        }

        it("uses Java match identities for an empty stream") {
            stream_t stream;
            bool matched = true;

            check_equal(STREAM_EMPTY(&stream, int), STREAM_OK);
            check_equal(stream.any_match(&stream, int_is_even, &matched), STREAM_END);
            check_false(matched);

            check_equal(STREAM_EMPTY(&stream, int), STREAM_OK);
            check_equal(stream.all_match(&stream, int_is_even, &matched), STREAM_END);
            check_true(matched);

            check_equal(STREAM_EMPTY(&stream, int), STREAM_OK);
            check_equal(stream.none_match(&stream, int_is_even, &matched), STREAM_END);
            check_true(matched);
        }

        it("returns AGAIN when a live terminal has no value now") {
            int storage = 0;
            uint64_t timestamp = 0;
            uint64_t sequence = 0;
            stream_live_ring_t ring;
            stream_t stream;
            size_t count = 99;
            bool matched = true;

            check_equal(stream_live_ring_init(
                             &ring, &storage, &timestamp, &sequence, 1,
                             sizeof(storage), STREAM_BP_REJECT_NEW),
                         STREAM_OK);
            check_equal(stream_from_live_ring(&stream, &ring), STREAM_OK);
            check_equal(stream.count(&stream, &count), STREAM_AGAIN);
            check_equal(count, 0);
            check_equal(
                stream.any_match(&stream, int_is_even, &matched),
                STREAM_AGAIN);
            check_false(matched);
        }

        it("fails to snapshot a non-clonable live source") {
            int storage = 0;
            uint64_t timestamp = 0;
            uint64_t sequence = 0;
            stream_live_ring_t ring;
            stream_t stream;
            stream_t snapshot;

            check_equal(stream_live_ring_init(
                             &ring, &storage, &timestamp, &sequence, 1,
                             sizeof(storage), STREAM_BP_REJECT_NEW),
                         STREAM_OK);
            check_equal(stream_from_live_ring(&stream, &ring), STREAM_OK);
            check_equal(stream_snapshot_init(&snapshot, &stream), STREAM_ERROR);
            check_equal(snapshot.error, STREAM_ERR_UNSUPPORTED_SOURCE);
        }

        it("supports limit as the Java name for take") {
            int values[] = {1, 2, 3};
            stream_array_source_state_t source_state;
            stream_t stream;
            size_t count = 0;

            check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
            check_equal(stream.limit(&stream, 2)->count(&stream, &count), STREAM_END);
            check_equal(count, 2);
            check_equal(source_state.pos, 2);
        }

        it("emits distinct values in first-seen order") {
            const int expected[] = {1, 2, 3};
            stream_t stream;
            stream_item_t item;
            int output = 0;
            size_t i;

            check_equal(STREAM_OF(&stream, int, 1, 2, 1, 3, 2), STREAM_OK);
            stream.distinct(&stream, 3, int_equal);

            for (i = 0; i < 3; ++i) {
                item.data = &output;
                item.size = sizeof(output);
                check_equal(stream.next(&stream, &item), STREAM_OK);
                check_equal(output, expected[i]);
            }
            item.size = sizeof(output);
            check_equal(stream.next(&stream, &item), STREAM_END);
        }

        it("resets distinct history") {
            stream_t stream;
            stream_item_t item;
            int output = 0;

            check_equal(STREAM_OF(&stream, int, 7, 7), STREAM_OK);
            stream.distinct(&stream, 1, int_equal);
            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream.next(&stream, &item), STREAM_OK);
            check_equal(stream.reset(&stream), STREAM_OK);
            item.size = sizeof(output);
            check_equal(stream.next(&stream, &item), STREAM_OK);
            check_equal(output, 7);
        }

        it("fails when distinct exceeds its unique-value limit") {
            stream_t stream;
            size_t count = 0;

            check_equal(STREAM_OF(&stream, int, 1, 2, 3), STREAM_OK);
            check_equal(
                stream.distinct(&stream, 2, int_equal)->count(&stream, &count),
                STREAM_ERROR);
            check_equal(count, 2);
            check_equal(stream.error, STREAM_ERR_DISTINCT_FULL);
            check_equal(
                stream_error_string(stream.error),
                "distinct unique-value limit reached");
        }

        it("finds minimum and maximum pipeline values") {
            stream_t stream;
            stream_item_t item;
            stream_result_t r;
            int output = 0;
            bool found = false;

            check_equal(STREAM_OF(&stream, int, 8, 3, 9, 4), STREAM_OK);
            item.data = &output;
            item.size = sizeof(output);
            r = stream.min_value(&stream, int_compare, &item, &found);
            check_equal(r, STREAM_END);
            check_true(found);
            check_equal(output, 3);
            check_equal(item.sequence, 1);

            check_equal(STREAM_OF(&stream, int, 8, 3, 9, 4), STREAM_OK);
            item.size = sizeof(output);
            r = stream.max_value(&stream, int_compare, &item, &found);
            check_equal(r, STREAM_END);
            check_true(found);
            check_equal(output, 9);
            check_equal(item.sequence, 2);
        }

        it("reports no minimum for an empty stream") {
            stream_t stream;
            stream_item_t item;
            int output = 0;
            bool found = true;

            check_equal(STREAM_EMPTY(&stream, int), STREAM_OK);
            item.data = &output;
            item.size = sizeof(output);
            check_equal(
                stream.min_value(&stream, int_compare, &item, &found),
                STREAM_END);
            check_false(found);
        }

        it("takes values while the predicate matches") {
            int values[] = {1, 2, 3, 4, 1};
            stream_array_source_state_t source_state;
            stream_t stream;
            size_t count = 0;

            check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
            check_equal(
                stream.take_while(&stream, int_less_than_four)->count(&stream, &count),
                STREAM_END);
            check_equal(count, 3);
            check_equal(source_state.pos, 4);

            count = 99;
            check_equal(stream.count(&stream, &count), STREAM_END);
            check_equal(count, 0);
            check_equal(source_state.pos, 4);
        }

        it("restores take_while state on reset") {
            stream_t stream;
            size_t count = 0;

            check_equal(STREAM_OF(&stream, int, 1, 2, 4, 3), STREAM_OK);
            stream.take_while(&stream, int_less_than_four);
            check_equal(stream.count(&stream, &count), STREAM_END);
            check_equal(count, 2);
            check_equal(stream.reset(&stream), STREAM_OK);
            check_equal(stream.count(&stream, &count), STREAM_END);
            check_equal(count, 2);
        }

        it("drops only the matching prefix") {
            const int expected[] = {3, 2, 1};
            int output[4] = {0};
            stream_t stream;
            size_t count = 0;

            drop_while_predicate_calls = 0;
            check_equal(STREAM_OF(&stream, int, 1, 2, 3, 2, 1), STREAM_OK);
            stream.drop_while(&stream, tracked_int_less_than_three);
            check_equal(STREAM_TO_ARRAY(&stream, output, &count), STREAM_END);
            check_equal(count, 3);
            check_equal(output, expected, sizeof(expected));
            check_equal(drop_while_predicate_calls, 3);
        }

        it("provides find_any with sequential find_first semantics") {
            stream_t stream;
            stream_item_t item;
            int output = 0;

            check_equal(STREAM_OF(&stream, int, 7, 8), STREAM_OK);
            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream.find_any(&stream, &item), STREAM_OK);
            check_equal(output, 7);
        }

        it("does not over-consume when to_array reaches capacity") {
            const int expected[] = {1, 2};
            int values[] = {1, 2, 3};
            int output[2] = {0};
            stream_array_source_state_t source_state;
            stream_t stream;
            stream_item_t item;
            size_t count = 0;
            int next = 0;

            check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
            check_equal(STREAM_TO_ARRAY(&stream, output, &count), STREAM_FULL);
            check_equal(count, 2);
            check_equal(output, expected, sizeof(expected));
            check_equal(source_state.pos, 2);

            item.data = &next;
            item.size = sizeof(next);
            check_equal(stream.next(&stream, &item), STREAM_OK);
            check_equal(next, 3);
        }

        it("rejects overflowing to_array capacity arithmetic") {
            int values[] = {1};
            int output = 0;
            stream_array_source_state_t source_state;
            stream_t stream;
            size_t count = 99;

            check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
            check_equal(
                stream.to_array(&stream, &output, SIZE_MAX, sizeof(output), &count),
                STREAM_ERROR);
            check_equal(stream.error, STREAM_ERR_BAD_ARGUMENT);
            check_equal(count, 0);
            check_equal(source_state.pos, 0);
        }

        it("collects into a caller-owned result object") {
            stream_t stream;
            int_summary_t summary = {0, 0};

            check_equal(STREAM_OF(&stream, int, 1, 2, 3, 4), STREAM_OK);
            check_equal(
                stream.collect(&stream, &summary, summarize_int),
                STREAM_END);
            check_equal(summary.sum, 10);
            check_equal(summary.count, 4);
        }

        it("records collect callback failures") {
            stream_t stream;
            int_summary_t summary = {0, 0};

            check_equal(STREAM_OF(&stream, int, 1), STREAM_OK);
            check_equal(
                stream.collect(&stream, &summary, fail_collect),
                STREAM_ERROR);
            check_equal(stream.error, STREAM_ERR_COLLECT_FAILED);
        }
    }

    group("take") {
        it("does not consume an item after reaching its limit") {
            int storage[3] = {0};
            uint64_t timestamps[3] = {0};
            uint64_t sequences[3] = {0};
            stream_live_ring_t ring;
            stream_t stream;
            stream_item_t item;
            int output = 0;

            check_equal(stream_live_ring_init(
                             &ring, storage, timestamps, sequences, 3,
                             sizeof(storage[0]), STREAM_BP_REJECT_NEW),
                         STREAM_OK);
            check_equal(stream_from_live_ring(&stream, &ring), STREAM_OK);
            stream_take(&stream, 1);

            check_equal(stream_live_ring_push(&ring, &(int){10}, 100), STREAM_PUSH_OK);
            check_equal(stream_live_ring_push(&ring, &(int){20}, 200), STREAM_PUSH_OK);
            check_equal(stream_live_ring_push(&ring, &(int){30}, 300), STREAM_PUSH_OK);

            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output, 10);
            check_equal(stream_next(&stream, &item), STREAM_END);
            check_equal(stream_live_ring_pending(&ring), 2);
        }
    }

    group("live ring validation") {
        it("rejects an unknown backpressure policy") {
            int storage = 0;
            uint64_t timestamp = 0;
            uint64_t sequence = 0;
            stream_live_ring_t ring;

            check_equal(stream_live_ring_init(
                             &ring, &storage, &timestamp, &sequence, 1,
                             sizeof(storage), (stream_backpressure_policy_t)INT_MAX),
                         STREAM_ERROR);
        }

        it("rejects capacity arithmetic overflow") {
            int storage = 0;
            uint64_t timestamp = 0;
            uint64_t sequence = 0;
            stream_live_ring_t ring;
            const size_t overflowing_capacity = SIZE_MAX / sizeof(storage) + 1;

            check_equal(stream_live_ring_init(
                             &ring, &storage, &timestamp, &sequence,
                             overflowing_capacity, sizeof(storage),
                             STREAM_BP_REJECT_NEW),
                         STREAM_ERROR);
        }
    }

    group("container invariants") {
        it("rejects array address arithmetic overflow") {
            int value = 1;
            stream_array_view_t view = stream_array_view(
                &value, SIZE_MAX / sizeof(value) + 1, sizeof(value));
            stream_t stream;

            check_equal(stream_from_array_view(&stream, &view), STREAM_ERROR);
        }

        it("detects vector modification during iteration") {
            int storage[2] = {0};
            stream_vector_t vector;
            stream_t stream;
            stream_item_t item;
            int output = 0;

            stream_vector_init(&vector, storage, 2, sizeof(storage[0]));
            check_equal(stream_vector_push_back(&vector, &(int){1}), STREAM_OK);
            check_equal(stream_from_vector(&stream, &vector), STREAM_OK);

            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(stream_vector_push_back(&vector, &(int){2}), STREAM_OK);
            check_equal(stream_next(&stream, &item), STREAM_MODIFIED);
            check_equal(stream.error, STREAM_ERR_SOURCE_MODIFIED);
        }

        it("rejects inserting the same intrusive node twice") {
            int value = 1;
            stream_list_t list;
            stream_list_node_t node;

            stream_list_init(&list, sizeof(value));
            stream_list_node_init(&node, &value);
            check_equal(stream_list_push_back(&list, &node), STREAM_OK);
            check_equal(stream_list_push_back(&list, &node), STREAM_ERROR);
            check_equal(list.size, 1);
            check_equal((const void *)(list.head), (const void *)(list.tail));
            check_null(list.head->next);
        }
    }

    group("stateful operator ownership") {
        it("rejects nesting borrowed window values") {
            int values[] = {1, 2, 3};
            stream_array_source_state_t source_state;
            stream_t stream;

            check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
            stream_window(&stream, 2);
            check_equal(stream.error, STREAM_ERR_NONE);
            stream_window(&stream, 2);
            check_equal(stream.error, STREAM_ERR_BAD_ARGUMENT);
            check_equal(stream.op_count, 1);
        }

        it("rejects debounce over a borrowed window") {
            int values[] = {1, 2, 3};
            stream_array_source_state_t source_state;
            stream_t stream;

            check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
            stream_window(&stream, 2);
            stream_debounce(&stream, 2, int_equal);
            check_equal(stream.error, STREAM_ERR_BAD_ARGUMENT);
            check_equal(stream.op_count, 1);
        }

        it("rejects distinct over a borrowed window") {
            stream_t stream;

            check_equal(STREAM_OF(&stream, int, 1, 2, 3), STREAM_OK);
            stream_window(&stream, 2);
            stream_distinct(&stream, 2, int_equal);
            check_equal(stream.error, STREAM_ERR_BAD_ARGUMENT);
            check_equal(stream.op_count, 1);
        }
    }

    group("ordered pipeline") {
        it("applies filter map skip and take in declaration order") {
            int values[] = {1, 2, 3, 4, 5, 6};
            stream_array_source_state_t source_state;
            stream_t stream;
            stream_item_t item;
            int output = 0;

            check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
            stream_filter(&stream, int_is_even);
            stream_map(&stream, sizeof(int), int_times_ten);
            stream_skip(&stream, 1);
            stream_take(&stream, 1);

            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output, 40);
            check_equal(stream_next(&stream, &item), STREAM_END);
            check_equal(source_state.pos, 4);
        }

        it("reset restores source and window state") {
            int values[] = {1, 2, 3};
            const int expected[] = {1, 2};
            stream_array_source_state_t source_state;
            stream_t stream;
            stream_item_t item;
            stream_window_t window;

            check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
            stream_window(&stream, 2);

            item.data = &window;
            item.size = sizeof(window);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(window.count, 2);
            check_equal(window.data, expected, sizeof(expected));

            check_equal(stream_reset(&stream), STREAM_OK);
            item.size = sizeof(window);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(window.data, expected, sizeof(expected));
            check_equal(window.first_sequence, 0);
            check_equal(window.last_sequence, 1);
        }

        it("count debounce emits only stable value changes") {
            int values[] = {1, 1, 1, 2, 2, 2};
            stream_array_source_state_t source_state;
            stream_t stream;
            stream_item_t item;
            int output = 0;

            check_equal(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
            stream_debounce(&stream, 3, int_equal);

            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output, 1);
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output, 2);
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_END);
        }

        it("time debounce uses timestamps and restarts after time moves backward") {
            int storage[4] = {0};
            uint64_t timestamps[4] = {0};
            uint64_t sequences[4] = {0};
            stream_live_ring_t ring;
            stream_t stream;
            stream_item_t item;
            int output = 0;

            check_equal(stream_live_ring_init(
                             &ring, storage, timestamps, sequences, 4,
                             sizeof(storage[0]), STREAM_BP_REJECT_NEW),
                         STREAM_OK);
            check_equal(stream_live_ring_push(&ring, &(int){5}, 100000000), STREAM_PUSH_OK);
            check_equal(stream_live_ring_push(&ring, &(int){5}, 90000000), STREAM_PUSH_OK);
            check_equal(stream_live_ring_push(&ring, &(int){5}, 150000000), STREAM_PUSH_OK);
            check_equal(stream_live_ring_push(&ring, &(int){5}, 190000000), STREAM_PUSH_OK);
            stream_live_ring_close_input(&ring);
            check_equal(stream_from_live_ring(&stream, &ring), STREAM_OK);
            stream_debounce_ms(&stream, 100, int_equal);

            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output, 5);
            check_equal(item.timestamp_ns, 190000000);
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_END);
        }
    }

    group("lifecycle") {
        it("closes a source at most once") {
            close_source_state_t state = {0};
            stream_source_t source = {
                &state,
                sizeof(int),
                empty_source_next,
                NULL,
                count_source_close
            };
            stream_t stream;

            check_equal(stream_init(&stream, &source), STREAM_OK);
            stream_close(&stream);
            stream_close(&stream);
            check_equal(state.close_count, 1);
            check_null(stream.source.close);
        }

        it("records source next failures") {
            stream_source_t source = {
                NULL,
                sizeof(int),
                failing_source_next,
                failing_source_reset,
                NULL
            };
            stream_t stream;
            stream_item_t item;
            int output = 0;

            check_equal(stream_init(&stream, &source), STREAM_OK);
            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_ERROR);
            check_equal(stream.error, STREAM_ERR_SOURCE_FAILED);
            check_equal(stream_error_string(stream.error), "source operation failed");
        }

        it("records source reset failures") {
            stream_source_t source = {
                NULL,
                sizeof(int),
                empty_source_next,
                failing_source_reset,
                NULL
            };
            stream_t stream;

            check_equal(stream_init(&stream, &source), STREAM_OK);
            check_equal(stream_reset(&stream), STREAM_ERROR);
            check_equal(stream.error, STREAM_ERR_SOURCE_FAILED);
        }
    }

    group("backpressure") {
        it("rejects a new item without modifying a full ring") {
            int storage[1] = {0};
            uint64_t timestamps[1] = {0};
            uint64_t sequences[1] = {0};
            stream_live_ring_t ring;

            check_equal(stream_live_ring_init(
                             &ring, storage, timestamps, sequences, 1,
                             sizeof(storage[0]), STREAM_BP_REJECT_NEW),
                         STREAM_OK);
            check_equal(stream_live_ring_push(&ring, &(int){1}, 10), STREAM_PUSH_OK);
            check_equal(stream_live_ring_push(&ring, &(int){2}, 20), STREAM_PUSH_FULL);
            check_equal(stream_live_ring_pending(&ring), 1);
            check_equal(storage[0], 1);
        }

        it("drains accepted items after input closes") {
            int storage[1] = {0};
            uint64_t timestamps[1] = {0};
            uint64_t sequences[1] = {0};
            stream_live_ring_t ring;
            stream_t stream;
            stream_item_t item;
            int output = 0;

            check_equal(stream_live_ring_init(
                             &ring, storage, timestamps, sequences, 1,
                             sizeof(storage[0]), STREAM_BP_REJECT_NEW),
                         STREAM_OK);
            check_equal(stream_live_ring_push(&ring, &(int){7}, 70), STREAM_PUSH_OK);
            stream_live_ring_close_input(&ring);
            check_equal(stream_from_live_ring(&stream, &ring), STREAM_OK);

            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output, 7);
            check_equal(stream_next(&stream, &item), STREAM_END);
            check_equal(stream_live_ring_push(&ring, &(int){8}, 80), STREAM_PUSH_ERROR);
        }

        it("drops the newest item when configured") {
            int storage[1] = {0};
            uint64_t timestamps[1] = {0};
            uint64_t sequences[1] = {0};
            stream_live_ring_t ring;

            check_equal(stream_live_ring_init(
                             &ring, storage, timestamps, sequences, 1,
                             sizeof(storage[0]), STREAM_BP_DROP_NEWEST),
                         STREAM_OK);
            check_equal(stream_live_ring_push(&ring, &(int){1}, 10), STREAM_PUSH_OK);
            check_equal(stream_live_ring_push(&ring, &(int){2}, 20), STREAM_PUSH_DROPPED);
            check_equal(storage[0], 1);
            check_equal(stream_live_ring_dropped(&ring), 1);
        }

        it("drops the oldest item and preserves FIFO order") {
            int storage[2] = {0};
            uint64_t timestamps[2] = {0};
            uint64_t sequences[2] = {0};
            stream_live_ring_t ring;
            stream_t stream;
            stream_item_t item;
            int output = 0;

            check_equal(stream_live_ring_init(
                             &ring, storage, timestamps, sequences, 2,
                             sizeof(storage[0]), STREAM_BP_DROP_OLDEST),
                         STREAM_OK);
            check_equal(stream_live_ring_push(&ring, &(int){1}, 10), STREAM_PUSH_OK);
            check_equal(stream_live_ring_push(&ring, &(int){2}, 20), STREAM_PUSH_OK);
            check_equal(stream_live_ring_push(&ring, &(int){3}, 30), STREAM_PUSH_DROPPED);
            check_equal(stream_from_live_ring(&stream, &ring), STREAM_OK);

            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output, 2);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output, 3);
            check_equal(stream_live_ring_dropped(&ring), 1);
        }

        it("latest-only coalesces all pending items") {
            int storage[3] = {0};
            uint64_t timestamps[3] = {0};
            uint64_t sequences[3] = {0};
            stream_live_ring_t ring;
            stream_t stream;
            stream_item_t item;
            int output = 0;

            check_equal(stream_live_ring_init(
                             &ring, storage, timestamps, sequences, 3,
                             sizeof(storage[0]), STREAM_BP_LATEST_ONLY),
                         STREAM_OK);
            check_equal(stream_live_ring_push(&ring, &(int){1}, 10), STREAM_PUSH_OK);
            check_equal(stream_live_ring_push(&ring, &(int){2}, 20), STREAM_PUSH_DROPPED);
            check_equal(stream_live_ring_push(&ring, &(int){3}, 30), STREAM_PUSH_DROPPED);
            check_equal(stream_live_ring_pending(&ring), 1);
            check_equal(stream_live_ring_dropped(&ring), 2);
            check_equal(stream_from_live_ring(&stream, &ring), STREAM_OK);

            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output, 3);
            check_equal(item.timestamp_ns, 30);
            check_equal(item.sequence, 2);
        }
    }

    group("spsc ring source") {
        it("rejects non-power-of-two storage for spsc ring") {
            uint8_t storage[24] = {0};
            stream_spsc_ring_t ring;

            check_equal(
                stream_spsc_ring_init(
                    &ring, storage, sizeof(storage), sizeof(int),
                    STREAM_BP_REJECT_NEW),
                STREAM_ERROR);
        }

        it("pushes and pops values with timestamp and sequence metadata") {
            uint8_t storage[64] = {0};
            stream_spsc_ring_t ring;
            stream_t stream;
            stream_item_t item;
            int output = 0;

            check_equal(
                stream_spsc_ring_init(
                    &ring, storage, sizeof(storage), sizeof(int),
                    STREAM_BP_REJECT_NEW),
                STREAM_OK);
            check_equal(stream_spsc_ring_push(&ring, &(int){10}, 100), STREAM_PUSH_OK);
            check_equal(stream_spsc_ring_push(&ring, &(int){20}, 200), STREAM_PUSH_OK);

            check_equal(stream_from_spsc_ring(&stream, &ring), STREAM_OK);
            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output, 10);
            check_equal(item.timestamp_ns, 100);
            check_equal(item.sequence, 0);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output, 20);
            check_equal(item.timestamp_ns, 200);
            check_equal(item.sequence, 1);
            check_equal(stream_next(&stream, &item), STREAM_AGAIN);
        }

        it("reports AGAIN before close and END after close when empty") {
            uint8_t storage[64] = {0};
            stream_spsc_ring_t ring;
            stream_t stream;
            stream_item_t item;
            int output = 0;

            check_equal(
                stream_spsc_ring_init(
                    &ring, storage, sizeof(storage), sizeof(int),
                    STREAM_BP_REJECT_NEW),
                STREAM_OK);
            check_equal(stream_from_spsc_ring(&stream, &ring), STREAM_OK);

            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_AGAIN);
            stream_spsc_ring_close_input(&ring);
            check_equal(stream_next(&stream, &item), STREAM_END);
        }

        it("rejects new values when full and closed output is drained") {
            uint8_t storage[64] = {0};
            stream_spsc_ring_t ring;

            check_equal(
                stream_spsc_ring_init(
                    &ring, storage, sizeof(storage), sizeof(int),
                    STREAM_BP_REJECT_NEW),
                STREAM_OK);
            check_equal(stream_spsc_ring_push(&ring, &(int){1}, 10), STREAM_PUSH_OK);
            check_equal(stream_spsc_ring_push(&ring, &(int){2}, 20), STREAM_PUSH_OK);
            check_equal(stream_spsc_ring_push(&ring, &(int){3}, 30), STREAM_PUSH_OK);
            check_equal(stream_spsc_ring_push(&ring, &(int){4}, 40), STREAM_PUSH_FULL);
            stream_spsc_ring_close_input(&ring);
            check_equal(stream_spsc_ring_pending(&ring), 3);
            check_equal(stream_spsc_ring_push(&ring, &(int){5}, 50), STREAM_PUSH_ERROR);
        }

        it("drops the oldest item when configured under spsc policy") {
            uint8_t storage[128] = {0};
            stream_spsc_ring_t ring;
            stream_t stream;
            stream_item_t item;
            typedef struct {
                int value;
                int32_t pad1;
                int32_t pad2;
                int32_t pad3;
            } spsc_value_t;
            spsc_value_t output = {0};

            check_equal(
                stream_spsc_ring_init(
                    &ring, storage, sizeof(storage), sizeof(spsc_value_t),
                    STREAM_BP_DROP_OLDEST),
                STREAM_OK);
            check_equal(stream_spsc_ring_push(
                             &ring,
                             &(spsc_value_t){.value = 1, .pad1 = 0, .pad2 = 0, .pad3 = 0},
                             10),
                         STREAM_PUSH_OK);
            check_equal(stream_spsc_ring_push(
                             &ring,
                             &(spsc_value_t){.value = 2, .pad1 = 0, .pad2 = 0, .pad3 = 0},
                             20),
                         STREAM_PUSH_OK);
            check_equal(stream_spsc_ring_push(
                             &ring,
                             &(spsc_value_t){.value = 3, .pad1 = 0, .pad2 = 0, .pad3 = 0},
                             30),
                         STREAM_PUSH_OK);
            check_equal(stream_spsc_ring_push(
                             &ring,
                             &(spsc_value_t){.value = 4, .pad1 = 0, .pad2 = 0, .pad3 = 0},
                             40),
                         STREAM_PUSH_DROPPED);

            check_equal(stream_from_spsc_ring(&stream, &ring), STREAM_OK);
            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output.value, 2);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output.value, 3);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output.value, 4);
            check_equal(stream_next(&stream, &item), STREAM_AGAIN);
            check_equal(stream_spsc_ring_dropped(&ring), 1);
            check_equal(stream_spsc_ring_pending(&ring), 0);
        }

        it("drops the newest item when configured under spsc policy") {
            uint8_t storage[128] = {0};
            stream_spsc_ring_t ring;
            stream_t stream;
            stream_item_t item;
            typedef struct {
                int value;
                int32_t pad1;
                int32_t pad2;
                int32_t pad3;
            } spsc_value_t;
            spsc_value_t output = {0};

            check_equal(
                stream_spsc_ring_init(
                    &ring, storage, sizeof(storage), sizeof(spsc_value_t),
                    STREAM_BP_DROP_NEWEST),
                STREAM_OK);
            check_equal(stream_spsc_ring_push(
                             &ring,
                             &(spsc_value_t){.value = 1, .pad1 = 0, .pad2 = 0, .pad3 = 0},
                             10),
                         STREAM_PUSH_OK);
            check_equal(stream_spsc_ring_push(
                             &ring,
                             &(spsc_value_t){.value = 2, .pad1 = 0, .pad2 = 0, .pad3 = 0},
                             20),
                         STREAM_PUSH_OK);
            check_equal(stream_spsc_ring_push(
                             &ring,
                             &(spsc_value_t){.value = 3, .pad1 = 0, .pad2 = 0, .pad3 = 0},
                             30),
                         STREAM_PUSH_OK);
            check_equal(stream_spsc_ring_push(
                             &ring,
                             &(spsc_value_t){.value = 4, .pad1 = 0, .pad2 = 0, .pad3 = 0},
                             40),
                         STREAM_PUSH_DROPPED);

            check_equal(stream_from_spsc_ring(&stream, &ring), STREAM_OK);
            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output.value, 1);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output.value, 2);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output.value, 3);
            check_equal(stream_next(&stream, &item), STREAM_AGAIN);
            check_equal(stream_spsc_ring_dropped(&ring), 1);
        }

        it("latest-only policy keeps only latest item and drops prior values") {
            uint8_t storage[64] = {0};
            stream_spsc_ring_t ring;
            stream_t stream;
            stream_item_t item;
            int output = 0;

            check_equal(
                stream_spsc_ring_init(
                    &ring, storage, sizeof(storage), sizeof(int),
                    STREAM_BP_LATEST_ONLY),
                STREAM_OK);
            check_equal(stream_spsc_ring_push(&ring, &(int){1}, 10), STREAM_PUSH_OK);
            check_equal(stream_spsc_ring_push(&ring, &(int){2}, 20), STREAM_PUSH_DROPPED);
            check_equal(stream_spsc_ring_push(&ring, &(int){3}, 30), STREAM_PUSH_DROPPED);
            check_equal(stream_spsc_ring_pending(&ring), 1);
            check_equal(stream_spsc_ring_dropped(&ring), 2);

            check_equal(stream_from_spsc_ring(&stream, &ring), STREAM_OK);
            item.data = &output;
            item.size = sizeof(output);
            check_equal(stream_next(&stream, &item), STREAM_OK);
            check_equal(output, 3);
            check_equal(item.sequence, 2);
            check_equal(item.timestamp_ns, 30);
            check_equal(stream_next(&stream, &item), STREAM_AGAIN);
        }
    }
}
