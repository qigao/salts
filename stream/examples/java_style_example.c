#include "stream_turbo_containers.h"

#include <stdio.h>

static bool is_even(const void *value)
{
    return *(const int *)value % 2 == 0;
}

static bool greater_than_four(const void *value)
{
    return *(const int *)value > 4;
}

static bool int_equal(const void *left, const void *right)
{
    return *(const int *)left == *(const int *)right;
}

static int int_compare(const void *left, const void *right)
{
    const int a = *(const int *)left;
    const int b = *(const int *)right;
    return (a > b) - (a < b);
}

static bool int64_is_negative(const void *value)
{
    return *(const int64_t *)value < 0;
}

static bool int64_less_than_four(const void *value)
{
    return *(const int64_t *)value < 4;
}

static void print_seen(const void *value)
{
    printf("peek: %d\n", *(const int *)value);
}

static const void *g_last_boxed = NULL;

static void print_boxed(const void *value)
{
    g_last_boxed = *(const void * const *)value;
    printf("boxed int: %d\n", *(const int *)g_last_boxed);
}

static stream_result_t add_int(void *accumulator, const void *value)
{
    *(int *)accumulator += *(const int *)value;
    return STREAM_OK;
}

typedef struct {
    int sum;
    size_t count;
} int_summary_t;

static stream_result_t emit_value_and_square(
    const void *input,
    stream_emitter_t *emitter)
{
    const int value = *(const int *)input;
    const int square = value * value;
    stream_result_t r = emitter->emit(emitter, &value);

    if (r != STREAM_OK) {
        return r;
    }
    return emitter->emit(emitter, &square);
}

static stream_result_t summarize_int(void *result, const void *value)
{
    int_summary_t *summary = (int_summary_t *)result;

    summary->sum += *(const int *)value;
    ++summary->count;
    return STREAM_OK;
}

static stream_result_t double_int(const void *input, void *output)
{
    *(int *)output = *(const int *)input * 2;
    return STREAM_OK;
}

int main(void)
{
    stream_t stream;
    stream_t *s = &stream;
    stream_t other;
    stream_item_t first;
    turbo_vec_t collected;
    stream_result_t r;
    int_summary_t summary = {0, 0};
    int64_t range_values[8] = {0};
    int powers[5] = {0};
    int concatenated[5] = {0};
    size_t range_count = 0;
    size_t powers_count = 0;
    size_t count = 0;
    int value = 0;
    int sum = 0;
    bool matched = false;
    bool found = false;

    if (STREAM_OF(s, int, 1, 2, 3, 4, 5, 6) != STREAM_OK) {
        return 1;
    }
    r = s->filter(s, is_even)->peek(s, print_seen)->count(s, &count);
    if (r != STREAM_END) {
        return 1;
    }
    printf("count: %zu\n", count);

    if (STREAM_OF(s, int, 1, 2, 3, 4) != STREAM_OK) {
        return 1;
    }
    r = s->reduce(s, &sum, add_int);
    if (r != STREAM_END) {
        return 1;
    }
    printf("sum: %d\n", sum);

    if (STREAM_OF(s, int, 1, 3, 5, 6, 7) != STREAM_OK) {
        return 1;
    }
    r = s->any_match(s, is_even, &matched);
    if (r != STREAM_OK || !matched) {
        return 1;
    }
    printf("any even: %s\n", matched ? "true" : "false");

    if (STREAM_OF(s, int, 1, 3, 4, 6) != STREAM_OK) {
        return 1;
    }
    first.data = &value;
    first.size = sizeof(value);
    r = s->filter(s, greater_than_four)->find_any(s, &first);
    if (r != STREAM_OK) {
        return 1;
    }
    printf("first > 4: %d\n", value);

    if (STREAM_OF(s, int, 3, 1, 3, 2, 1) != STREAM_OK) {
        return 1;
    }
    r = s->distinct(s, 3, int_equal)->count(s, &count);
    if (r != STREAM_END) {
        return 1;
    }
    printf("distinct count: %zu\n", count);

    if (STREAM_OF(s, int, 8, 3, 9, 4) != STREAM_OK) {
        return 1;
    }
    first.data = &value;
    first.size = sizeof(value);
    r = s->min_value(s, int_compare, &first, &found);
    if (r != STREAM_END || !found) {
        return 1;
    }
    printf("min: %d\n", value);

    if (turbo_vec_init(&collected, sizeof(int)) != TURBO_OK) {
        return 1;
    }
    if (STREAM_OF(s, int, 10, 20, 30) != STREAM_OK) {
        turbo_vec_destroy(&collected);
        return 1;
    }
    r = stream_collect_turbo_vec(s, &collected, 4);
    if (r != STREAM_END) {
        turbo_vec_destroy(&collected);
        return 1;
    }
    printf("collected: %zu\n", turbo_vec_size(&collected));
    turbo_vec_destroy(&collected);

    if (STREAM_RANGE(s, -2, 10) != STREAM_OK) {
        return 1;
    }
    r = STREAM_TO_ARRAY(
        s->drop_while(s, int64_is_negative)->take_while(s, int64_less_than_four),
        range_values, &range_count);
    if (r != STREAM_END || range_count != 4) {
        return 1;
    }
    printf(
        "range: %lld..%lld\n",
        (long long)range_values[0],
        (long long)range_values[range_count - 1]);

    if (STREAM_OF(s, int, 2, 3) != STREAM_OK) {
        return 1;
    }
    r = s->flat_map(s, sizeof(int), 4, emit_value_and_square)
         ->sorted(s, 4, int_compare)
         ->collect(s, &summary, summarize_int);
    if (r != STREAM_END || summary.count != 4 || summary.sum != 18) {
        return 1;
    }
    printf("flat sorted sum: %d\n", summary.sum);

    if (STREAM_ITERATE(s, int, 1, 4, double_int) != STREAM_OK) {
        return 1;
    }
    r = STREAM_TO_ARRAY(s, powers, &powers_count);
    if (r != STREAM_END || powers_count != 4) {
        return 1;
    }
    printf("iterate last: %d\n", powers[powers_count - 1]);

    if (STREAM_OF(s, int, 1, 3) != STREAM_OK ||
        STREAM_OF(&other, int, 2, 4) != STREAM_OK) {
        return 1;
    }
    count = 0;
    r = s->concat(s, &other, 8)->to_array(
        s, concatenated, 5, sizeof(concatenated[0]), &count);
    if (r != STREAM_END || count != 4) {
        return 1;
    }
    printf("concat count: %zu\n", count);

    if (STREAM_OF(s, int, 10, 20, 30, 40) != STREAM_OK) {
        return 1;
    }
    r = s->skip(s, 2)->peek(s, print_seen)->boxed(s)->for_each(s, print_boxed);
    if (r != STREAM_END) {
        return 1;
    }
    if (g_last_boxed == NULL) {
        return 1;
    }
    printf("last boxed int: %d\n", *(const int *)g_last_boxed);

    s->close(s);
    return 0;
}
