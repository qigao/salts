#include "stream.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    int age;
} User;

static bool is_adult(const void *value)
{
    const User *u = (const User *)value;
    return u->age >= 18;
}

static stream_result_t user_to_name(const void *input, void *output)
{
    const User *u = (const User *)input;
    const char **name = (const char **)output;
    *name = u->name;
    return STREAM_OK;
}

static bool long_name(const void *value)
{
    const char *const *name = (const char *const *)value;
    return strlen(*name) >= 4;
}

static stream_result_t name_to_length(const void *input, void *output)
{
    const char *const *name = (const char *const *)input;
    int *len = (int *)output;
    *len = (int)strlen(*name);
    return STREAM_OK;
}

static void print_int(const void *value)
{
    printf("%d\n", *(const int *)value);
}

int main(void)
{
    stream_t stream;
    stream_t *s = &stream;
    stream_result_t r;

    r = STREAM_OF(
        s,
        User,
        (User){"Alice", 16},
        (User){"Bob", 22},
        (User){"Carol", 35},
        (User){"Dave", 28},
        (User){"Eve", 31});
    if (r != STREAM_OK) {
        fprintf(stderr, "stream init failed\n");
        return 1;
    }

    s->filter(s, is_adult)
     ->map(s, sizeof(const char *), user_to_name)
     ->take(s, 3)
     ->filter(s, long_name)
     ->map(s, sizeof(int), name_to_length)
     ->for_each(s, print_int);

    if (s->error != STREAM_ERR_NONE) {
        fprintf(stderr, "stream error: %s\n", stream_error_string(s->error));
        return 1;
    }

    puts("-- reset and run again --");

    if (s->reset(s) != STREAM_OK) {
        fprintf(stderr, "reset failed\n");
        return 1;
    }

    s->for_each(s, print_int);

    s->close(s);
    return 0;
}
