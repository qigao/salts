#include "stream_turbo_containers.h"

#include <stdint.h>
#include <stdio.h>

static bool int_is_even(const void *value)
{
    return *(const int *)value % 2 == 0;
}

static stream_result_t to_value(const void *value, void *output)
{
    *(int *)output = *(const int *)value;
    return STREAM_OK;
}

static stream_result_t sum_reducer(void *accumulator, const void *value)
{
    *(int *)accumulator += *(const int *)value;
    return STREAM_OK;
}

int main(void)
{
    const int input[] = {10, 11, 12, 20, 21, 22, 30};
    stream_t stream;
    turbo_list_t source;
    turbo_map_t partitions;
    const int *false_sum;
    const int *true_sum;

    if (turbo_list_from_array(
            &source, input, sizeof(input) / sizeof(input[0]), sizeof(input[0])) !=
        TURBO_OK) {
        return 1;
    }

    if (turbo_map_init(
            &partitions, sizeof(uint8_t), sizeof(int), NULL, NULL, NULL) !=
        TURBO_OK) {
        turbo_list_destroy(&source);
        return 1;
    }

    if (!STREAM_FROM_TURBO_LIST(&stream, &source) ||
        stream_collect_turbo_partition_reduce(
            &stream,
            &partitions,
            4,
            sizeof(int),
            int_is_even,
            to_value,
            sum_reducer) != STREAM_END) {
        turbo_map_destroy(&partitions);
        turbo_list_destroy(&source);
        return 1;
    }

    false_sum = (const int *)turbo_map_get_const(&partitions, &(uint8_t){0});
    true_sum = (const int *)turbo_map_get_const(&partitions, &(uint8_t){1});
    if (!false_sum || !true_sum) {
        turbo_map_destroy(&partitions);
        turbo_list_destroy(&source);
        return 1;
    }

    printf("odd_sum=%d even_sum=%d\n", *false_sum, *true_sum);

    turbo_map_destroy(&partitions);
    turbo_list_destroy(&source);
    return 0;
}
