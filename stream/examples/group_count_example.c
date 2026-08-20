#include "stream_turbo_containers.h"

#include <stdio.h>

static stream_result_t tens_bucket(const void *value, void *out_key)
{
    *(int *)out_key = *(const int *)value / 10;
    return STREAM_OK;
}

int main(void)
{
    stream_t stream;
    turbo_map_t counts;
    size_t i;

    if (turbo_map_init(&counts, sizeof(int), sizeof(size_t), NULL, NULL, NULL) !=
        TURBO_OK) {
        return 1;
    }

    if (STREAM_OF(&stream, int, 10, 11, 18, 24, 27, 34, 39, 41) != STREAM_OK) {
        turbo_map_destroy(&counts);
        return 1;
    }

    if (stream_collect_turbo_map_count(
            &stream,
            &counts,
            10,
            sizeof(int),
            tens_bucket) != STREAM_END) {
        turbo_map_destroy(&counts);
        return 1;
    }

    for (i = 0; i < turbo_map_size(&counts); ++i) {
        const int *bucket = (const int *)turbo_map_key_at(&counts, i);
        const size_t *count = (const size_t *)turbo_map_value_at_const(&counts, i);

        if (!bucket || !count) {
            turbo_map_destroy(&counts);
            return 1;
        }
        printf("bucket=%d, count=%zu\n", *bucket, *count);
    }

    turbo_map_destroy(&counts);
    return 0;
}
