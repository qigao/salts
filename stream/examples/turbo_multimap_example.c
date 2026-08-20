#include "stream_turbo_containers.h"

#include <stdio.h>

TURBO_MULTI_MAP_DEFINE(int_int_multimap_t, int, int)

static bool is_even(const void *value)
{
    return *(const int *)value % 2 == 0;
}

static bool int_equal(const void *left, const void *right)
{
    return *(const int *)left == *(const int *)right;
}

static void print_int(const void *value)
{
    printf("%d ", *(const int *)value);
}

int main(void)
{
    int_int_multimap_t entries;
    int_int_multimap_t_entry source[] = {
        {1, 10}, {2, 21}, {1, 11}, {3, 30}, {2, 22}
    };
    stream_t stream;
    stream_result_t result;

    if (int_int_multimap_t_from(&entries, source, sizeof(source) / sizeof(source[0])) !=
        TURBO_OK) {
        return 1;
    }

    printf("values flattened: ");
    if (stream_from_turbo_multimap_values(&stream, &entries.raw) != STREAM_OK) {
        int_int_multimap_t_destroy(&entries);
        return 1;
    }
    result = stream.for_each(&stream, print_int);
    if (result != STREAM_END) {
        int_int_multimap_t_destroy(&entries);
        return 1;
    }
    puts("");

    printf("distinct keys: ");
    if (stream_from_turbo_multimap_keys(&stream, &entries.raw) != STREAM_OK) {
        int_int_multimap_t_destroy(&entries);
        return 1;
    }
    result = stream.distinct(&stream, 8, int_equal)->for_each(&stream, print_int);
    if (result != STREAM_END) {
        int_int_multimap_t_destroy(&entries);
        return 1;
    }
    puts("");

    printf("only even values: ");
    if (stream_from_turbo_multimap_values(&stream, &entries.raw) != STREAM_OK) {
        int_int_multimap_t_destroy(&entries);
        return 1;
    }
    result = stream.filter(&stream, is_even)->for_each(&stream, print_int);
    if (result != STREAM_END) {
        int_int_multimap_t_destroy(&entries);
        return 1;
    }
    puts("");

    int_int_multimap_t_destroy(&entries);
    return 0;
}
