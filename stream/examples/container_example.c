#include "stream.h"
#include "stream_container.h"

#include <stdio.h>

typedef struct {
    const char *name;
    int age;
} User;

static bool adult(const void *value)
{
    return ((const User *)value)->age >= 18;
}

static void print_user(const void *value)
{
    const User *u = (const User *)value;
    printf("%s(%d) ", u->name, u->age);
}

static void run_array_demo(void)
{
    User users[] = {
        { "Alice", 16 }, { "Bob", 22 }, { "Carol", 35 },
        { "Dave", 28 }, { "Eve", 31 }
    };
    stream_array_view_t view = stream_array_view(
        users, sizeof(users) / sizeof(users[0]), sizeof(users[0]));
    stream_t a;
    stream_t b;
    stream_t *ap = &a;
    stream_t *bp = &b;

    stream_from_array_view(&a, &view);
    stream_from_array_view(&b, &view);

    printf("array stream A: ");
    ap->filter(ap, adult)->take(ap, 2)->for_each(ap, print_user);
    puts("");

    printf("array stream B: ");
    bp->skip(bp, 1)->take(bp, 3)->for_each(bp, print_user);
    puts("");
}

static void run_vector_demo(void)
{
    User storage[8];
    User seed[] = {
        { "Alice", 16 }, { "Bob", 22 }, { "Carol", 35 },
        { "Dave", 28 }, { "Eve", 31 }
    };
    stream_vector_t users;
    stream_t a;
    stream_t b;
    stream_t fail_fast;
    stream_t *ap = &a;
    stream_t *bp = &b;
    stream_t *fail_fast_p = &fail_fast;
    size_t i;

    stream_vector_init(&users, storage,
                       sizeof(storage) / sizeof(storage[0]),
                       sizeof(storage[0]));

    for (i = 0; i < sizeof(seed) / sizeof(seed[0]); ++i) {
        stream_vector_push_back(&users, &seed[i]);
    }

    /* Two streams over one container have independent cursors. */
    stream_from_vector(&a, &users);
    stream_from_vector(&b, &users);

    printf("vector stream A: ");
    ap->filter(ap, adult)->take(ap, 2)->for_each(ap, print_user);
    puts("");

    printf("vector stream B: ");
    bp->skip(bp, 2)->take(bp, 2)->for_each(bp, print_user);
    puts("");

    /* Structural mutation invalidates a cursor created before the mutation. */
    stream_from_vector(&fail_fast, &users);
    {
        User out;
        stream_item_t item = { &out, sizeof(out), 0, 0 };
        User frank = { "Frank", 40 };
        stream_result_t r;

        r = fail_fast_p->next(fail_fast_p, &item);
        printf("fail-fast first: %s(%d)\n", out.name, out.age);
        if (r != STREAM_OK) {
            puts("unexpected first read failure");
            return;
        }

        stream_vector_push_back(&users, &frank);
        r = fail_fast_p->next(fail_fast_p, &item);
        printf("after vector mutation: %s\n",
               r == STREAM_MODIFIED ? "STREAM_MODIFIED" : "unexpected result");

        /* reset establishes a new cursor/version and the stream can run again. */
        fail_fast_p->reset(fail_fast_p);
        printf("after reset: ");
        fail_fast_p->for_each(fail_fast_p, print_user);
        puts("");
    }
}

static void run_list_demo(void)
{
    User values[] = {
        { "Alice", 16 }, { "Bob", 22 }, { "Carol", 35 }
    };
    stream_list_node_t nodes[3];
    stream_list_t list;
    stream_t stream;
    stream_t *stream_p = &stream;
    size_t i;

    stream_list_init(&list, sizeof(User));
    for (i = 0; i < 3; ++i) {
        stream_list_node_init(&nodes[i], &values[i]);
        stream_list_push_back(&list, &nodes[i]);
    }

    stream_from_list(&stream, &list);
    printf("list stream: ");
    stream_p->filter(stream_p, adult)->for_each(stream_p, print_user);
    puts("");
}

int main(void)
{
    run_array_demo();
    run_vector_demo();
    run_list_demo();
    return 0;
}
