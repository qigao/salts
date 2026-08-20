#include "stream.h"
#include "stream_live.h"

#include <stdbool.h>
#include <stdio.h>

static void print_int(const void *value)
{
    printf("%d ", *(const int *)value);
}

static bool int_equal(const void *a, const void *b)
{
    return *(const int *)a == *(const int *)b;
}

static bool is_even(const void *value)
{
    return (*(const int *)value % 2) == 0;
}

static stream_result_t window_sum(const void *input, void *output)
{
    const stream_window_t *window = (const stream_window_t *)input;
    const int *values = (const int *)window->data;
    int sum = 0;
    size_t i;

    for (i = 0; i < window->count; ++i) {
        sum += values[i];
    }

    *(int *)output = sum;
    return STREAM_OK;
}

static const char *result_name(stream_result_t r)
{
    switch (r) {
    case STREAM_OK: return "STREAM_OK";
    case STREAM_END: return "STREAM_END";
    case STREAM_AGAIN: return "STREAM_AGAIN";
    case STREAM_MODIFIED: return "STREAM_MODIFIED";
    case STREAM_ERROR: return "STREAM_ERROR";
    default: return "?";
    }
}

static void window_demo(void)
{
    stream_t s;
    stream_t *stream = &s;
    stream_result_t r;

    if (STREAM_OF(&s, int, 1, 2, 3, 4, 5) != STREAM_OK) {
        return;
    }
    stream->window(stream, 3)
          ->map(stream, sizeof(int), window_sum);

    printf("window(3) sums: ");
    r = stream->for_each(stream, print_int);
    printf("=> %s\n", result_name(r));
}

static void debounce_demo(void)
{
    stream_t s;
    stream_t *stream = &s;
    stream_result_t r;

    if (STREAM_OF(&s, int, 1, 1, 2, 2, 2, 2, 1, 1, 1) != STREAM_OK) {
        return;
    }
    stream->debounce(stream, 2, int_equal);

    printf("debounce(2): ");
    r = stream->for_each(stream, print_int);
    printf("=> %s\n", result_name(r));
}


static void time_debounce_demo(void)
{
    int storage[8];
    uint64_t timestamps[8];
    uint64_t sequences[8];
    stream_live_ring_t ring;
    stream_t s;
    stream_t *stream = &s;
    stream_result_t r;
    int open = 1;
    int closed = 2;

    stream_live_ring_init(&ring, storage, timestamps, sequences,
                          8, sizeof(int), STREAM_BP_REJECT_NEW);
    stream_from_live_ring(&s, &ring);
    stream->debounce_ms(stream, 80, int_equal);

    stream_live_ring_push(&ring, &open,   0ULL * 1000000ULL);
    stream_live_ring_push(&ring, &open,  40ULL * 1000000ULL);
    stream_live_ring_push(&ring, &open,  90ULL * 1000000ULL); /* emits 1 */
    stream_live_ring_push(&ring, &closed, 100ULL * 1000000ULL);
    stream_live_ring_push(&ring, &closed, 150ULL * 1000000ULL);
    stream_live_ring_push(&ring, &closed, 190ULL * 1000000ULL); /* emits 2 */
    stream_live_ring_close_input(&ring);

    printf("debounce_ms(80): ");
    r = stream->for_each(stream, print_int);
    printf("=> %s\n", result_name(r));
}

static void live_demo(void)
{
    int storage[3];
    uint64_t timestamps[3];
    uint64_t sequences[3];
    stream_live_ring_t ring;
    stream_t s;
    stream_t *stream = &s;
    stream_item_t item;
    stream_result_t r;
    int a = 1, b = 2, c = 3, d = 4, e = 6, f = 8;

    stream_live_ring_init(&ring,
                          storage,
                          timestamps,
                          sequences,
                          3,
                          sizeof(int),
                          STREAM_BP_DROP_OLDEST);
    stream_from_live_ring(&s, &ring);
    stream->filter(stream, is_even);

    r = stream->next_view(stream, &item);
    printf("live empty: %s\n", result_name(r));

    stream_live_ring_push(&ring, &a, 100);
    stream_live_ring_push(&ring, &b, 200);
    stream_live_ring_push(&ring, &c, 300);
    stream_live_ring_push(&ring, &d, 400); /* drops oldest: 1 */

    printf("live pending=%zu dropped=%llu: ",
           stream_live_ring_pending(&ring),
           (unsigned long long)stream_live_ring_dropped(&ring));
    r = stream->for_each(stream, print_int);
    printf("=> %s\n", result_name(r));

    stream_live_ring_push(&ring, &e, 500);
    stream_live_ring_push(&ring, &f, 600);
    stream_live_ring_close_input(&ring);

    printf("live after close: ");
    r = stream->for_each(stream, print_int);
    printf("=> %s\n", result_name(r));
}

int main(void)
{
    window_demo();
    debounce_demo();
    time_debounce_demo();
    live_demo();
    return 0;
}
