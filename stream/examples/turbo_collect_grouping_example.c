#include "stream_turbo_containers.h"

#include <stdio.h>

static stream_result_t to_parity_key(const void *value, void *out_key)
{
    *(int *)out_key = *(const int *)value % 2;
    return STREAM_OK;
}

static stream_result_t pass_value(const void *value, void *out_value)
{
    *(int *)out_value = *(const int *)value;
    return STREAM_OK;
}

static void print_group(const char *label, const turbo_vec_t *values)
{
    size_t index;
    const int *value;
    if (!values) {
        printf("%s[]\n", label);
        return;
    }

    printf("%s[", label);
    for (index = 0; index < turbo_vec_size(values); ++index) {
        value = (const int *)turbo_vec_at_const(values, index);
        if (!value) {
            continue;
        }
        if (index > 0) {
            printf(" ");
        }
        printf("%d", value[0]);
    }
    puts("]");
}

int main(void)
{
    const int numbers[] = {10, 11, 20, 31, 40, 21, 35, 42};
    turbo_list_t source;
    turbo_map_t frequencies;
    turbo_multimap_t grouped_values;
    stream_t stream;
    const turbo_vec_t *even_values;
    const turbo_vec_t *odd_values;

    if (turbo_list_from_array(&source, numbers, sizeof(numbers) / sizeof(numbers[0]), sizeof(int)) !=
        TURBO_OK) {
        return 1;
    }

    if (turbo_map_init(
            &frequencies, sizeof(int), sizeof(size_t), NULL, NULL, NULL) !=
        TURBO_OK) {
        turbo_list_destroy(&source);
        return 1;
    }

    if (!STREAM_FROM_TURBO_LIST(&stream, &source) ||
        stream_collect_turbo_map_count(&stream, &frequencies, 8, sizeof(int), to_parity_key) !=
            STREAM_END) {
        turbo_map_destroy(&frequencies);
        turbo_list_destroy(&source);
        return 1;
    }

    printf("frequency even=%zu odd=%zu\n",
           *(const size_t *)turbo_map_get_const(&frequencies, &(int){0}),
           *(const size_t *)turbo_map_get_const(&frequencies, &(int){1}));
    turbo_map_destroy(&frequencies);

    if (turbo_multimap_init(&grouped_values, sizeof(int), sizeof(int), NULL, NULL, NULL) !=
        TURBO_OK) {
        turbo_list_destroy(&source);
        return 1;
    }

    if (!STREAM_FROM_TURBO_LIST(&stream, &source) ||
        stream_collect_turbo_multimap(&stream, &grouped_values, 16, sizeof(int),
                                     sizeof(int), to_parity_key, pass_value) !=
            STREAM_END) {
        turbo_multimap_destroy(&grouped_values);
        turbo_list_destroy(&source);
        return 1;
    }

    printf("grouped values:\n");
    even_values = turbo_multimap_get_values_const(&grouped_values, &(int){0});
    odd_values = turbo_multimap_get_values_const(&grouped_values, &(int){1});
    print_group(" even: ", even_values);
    print_group(" odd : ", odd_values);

    turbo_multimap_destroy(&grouped_values);
    turbo_list_destroy(&source);
    return 0;
}
