#include "stream.h"
#include "stream_streamable.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ---------------- Application-owned container ---------------- */

typedef struct {
    const char *name;
    int age;
} User;

#define USER_RING_CAPACITY 8

typedef struct {
    User data[USER_RING_CAPACITY];
    size_t head;
    size_t size;
    uint64_t version;
} user_ring_t;

static void user_ring_init(user_ring_t *ring)
{
    ring->head = 0;
    ring->size = 0;
    ring->version = 0;
}

static stream_result_t user_ring_push(user_ring_t *ring, User value)
{
    size_t index;

    if (!ring || ring->size >= USER_RING_CAPACITY) {
        return STREAM_ERROR;
    }

    index = (ring->head + ring->size) % USER_RING_CAPACITY;
    ring->data[index] = value;
    ++ring->size;
    ++ring->version;
    return STREAM_OK;
}

static stream_result_t user_ring_pop(user_ring_t *ring, User *out)
{
    if (!ring || ring->size == 0) {
        return STREAM_END;
    }

    if (out) {
        *out = ring->data[ring->head];
    }

    ring->head = (ring->head + 1) % USER_RING_CAPACITY;
    --ring->size;
    ++ring->version;
    return STREAM_OK;
}

/* ---------------- Only traversal logic is application-specific ---------------- */

typedef struct {
    bool started;
    size_t offset;
    uint64_t expected_version;
} user_ring_cursor_t;

static stream_result_t user_ring_next_ref(
    const user_ring_t *ring,
    user_ring_cursor_t *cursor,
    const User **out)
{
    size_t index;

    if (!ring || !cursor || !out) {
        return STREAM_ERROR;
    }

    /* A zeroed cursor represents a fresh traversal. */
    if (!cursor->started) {
        cursor->started = true;
        cursor->offset = 0;
        cursor->expected_version = ring->version;
    }

    if (cursor->expected_version != ring->version) {
        return STREAM_MODIFIED;
    }

    if (cursor->offset >= ring->size) {
        return STREAM_END;
    }

    index = (ring->head + cursor->offset) % USER_RING_CAPACITY;
    *out = &ring->data[index];
    ++cursor->offset;
    return STREAM_OK;
}

/*
 * This single declaration turns user_ring_t into a stream source.
 * The generated cursor wrapper, reset, stream_item copy and sequence metadata
 * are handled by the library adapter.
 */
STREAMABLE_REF(user_ring, user_ring_t, user_ring_cursor_t, User, user_ring_next_ref)

/* ---------------- Pipeline callbacks ---------------- */

static bool adult(const void *value)
{
    return ((const User *)value)->age >= 18;
}

static void print_user(const void *value)
{
    const User *u = (const User *)value;
    printf("%s(%d) ", u->name, u->age);
}

static void fill_ring(user_ring_t *ring)
{
    static const User seed[] = {
        { "Alice", 16 },
        { "Bob", 22 },
        { "Carol", 35 },
        { "Dave", 28 },
        { "Eve", 31 }
    };
    size_t i;

    user_ring_init(ring);
    for (i = 0; i < sizeof(seed) / sizeof(seed[0]); ++i) {
        user_ring_push(ring, seed[i]);
    }
}

int main(void)
{
    user_ring_t ring;
    stream_t adults;
    stream_t tail;
    stream_t fail_fast;
    stream_t *adults_p = &adults;
    stream_t *tail_p = &tail;
    stream_t *fail_fast_p = &fail_fast;

    fill_ring(&ring);

    /* Same custom container, independent per-stream cursors. */
    user_ring_stream(&adults, &ring);
    user_ring_stream(&tail, &ring);

    printf("ring adults: ");
    adults_p->filter(adults_p, adult)
            ->take(adults_p, 2)
            ->for_each(adults_p, print_user);
    puts("");

    printf("ring tail: ");
    tail_p->skip(tail_p, 2)
          ->for_each(tail_p, print_user);
    puts("");

    /* Structural mutation invalidates an already-started traversal. */
    user_ring_stream(&fail_fast, &ring);
    {
        User removed;
        stream_item_t item = { 0 };
        stream_result_t r;

        /* next_view() exposes the borrowed container element without copying. */
        r = fail_fast_p->next_view(fail_fast_p, &item);
        if (r != STREAM_OK) {
            puts("unexpected first read failure");
            return 1;
        }
        {
            const User *first = (const User *)item.data;
            printf("first borrowed view: %s(%d)\n", first->name, first->age);
        }

        user_ring_pop(&ring, &removed);
        r = fail_fast_p->next(fail_fast_p, &item);
        printf("after ring mutation: %s\n",
               r == STREAM_MODIFIED ? "STREAM_MODIFIED" : "unexpected result");

        /* reset zeros the generated cursor; next traversal snapshots new version. */
        fail_fast_p->reset(fail_fast_p);
        printf("after reset: ");
        fail_fast_p->for_each(fail_fast_p, print_user);
        puts("");
    }

    return 0;
}
