#include "stream_turbo_containers.h"

#include <stdint.h>
#include <stdio.h>

static bool is_even(const void *value)
{
    return *(const int *)value % 2 == 0;
}

int main(void)
{
    const int input[] = {1, 2, 3, 4, 5, 6, 7, 8};
    stream_t stream;
    turbo_list_t source;
    turbo_map_t counts;
    const size_t *even_count;
    const size_t *odd_count;
    uint8_t even_key = 1;
    uint8_t odd_key = 0;

    if (turbo_list_from_array(
            &source, input, sizeof(input) / sizeof(input[0]), sizeof(input[0])) !=
        TURBO_OK) {
        return 1;
    }

    if (turbo_map_init(&counts, sizeof(uint8_t), sizeof(size_t), NULL, NULL, NULL) !=
        TURBO_OK) {
        turbo_list_destroy(&source);
        return 1;
    }

    if (!STREAM_FROM_TURBO_LIST(&stream, &source) ||
        stream_collect_turbo_partition_count(&stream, &counts, 4, is_even) !=
            STREAM_END) {
        turbo_map_destroy(&counts);
        turbo_list_destroy(&source);
        return 1;
    }

    even_count = (const size_t *)turbo_map_get_const(&counts, &even_key);
    odd_count = (const size_t *)turbo_map_get_const(&counts, &odd_key);
    if (!even_count || !odd_count) {
        turbo_map_destroy(&counts);
        turbo_list_destroy(&source);
        return 1;
    }

    printf("even=%zu odd=%zu\n", *even_count, *odd_count);
    turbo_map_destroy(&counts);
    turbo_list_destroy(&source);
    return 0;
}
