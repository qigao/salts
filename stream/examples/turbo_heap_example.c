#include "stream_turbo_containers.h"

#include <stdio.h>

static int int_cmp(const void *left, const void *right, void *ctx)
{
    const int lhs = *(const int *)left;
    const int rhs = *(const int *)right;

    (void)ctx;
    return lhs < rhs ? -1 : lhs > rhs ? 1 : 0;
}

TURBO_HEAP_DEFINE(int_heap_t, int, int_cmp)

static stream_result_t square_if_small(const void *input, void *output)
{
    int value = *(const int *)input;
    *(int *)output = value * value;
    return STREAM_OK;
}

static void print_int(const void *value)
{
    printf("%d ", *(const int *)value);
}

int main(void)
{
    int source[] = {5, 2, 8, 1, 3};
    int_heap_t heap;
    stream_t stream;
    stream_result_t result;

    if (int_heap_t_from(&heap, source, (size_t)(sizeof(source) / sizeof(source[0]))) !=
        TURBO_OK) {
        return 1;
    }

    if (stream_from_turbo_heap(&stream, &heap.raw) != STREAM_OK) {
        return 1;
    }

    puts("turbo heap internal-order squared values:");
    stream_t *s = &stream;
    result = s->map(s, sizeof(int), square_if_small)->for_each(s, print_int);
    if (result != STREAM_END) {
        int_heap_t_destroy(&heap);
        return 1;
    }
    puts("");

    int_heap_t_destroy(&heap);
    return stream.error == STREAM_ERR_NONE ? 0 : 1;
}
