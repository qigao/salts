#include "stream_turbo_containers.h"

#include <stdio.h>

TURBO_VEC_DEFINE(int_vec_t, int)

static bool is_even(const void *value)
{
    return *(const int *)value % 2 == 0;
}

static stream_result_t square(const void *input, void *output)
{
    int value = *(const int *)input;
    *(int *)output = value * value;
    return STREAM_OK;
}

static void print_int(const void *value)
{
    printf("%d\n", *(const int *)value);
}

int main(void)
{
    const int source[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    int_vec_t values;
    stream_t stream;
    stream_t *s = &stream;

    if (int_vec_t_from(
            &values, source, sizeof(source) / sizeof(source[0])) != TURBO_OK) {
        return 1;
    }
    if (stream_from_turbo_vec(s, &values.raw) != STREAM_OK) {
        int_vec_t_destroy(&values);
        return 1;
    }

    s->filter(s, is_even)
     ->map(s, sizeof(int), square)
     ->take(s, 3)
     ->for_each(s, print_int);

    int_vec_t_destroy(&values);
    return s->error == STREAM_ERR_NONE ? 0 : 1;
}
