#include "stream.h"
#include "stream_typed.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    int age;
} User;

typedef struct {
    const char *text;
} Name;

static bool adult_typed(const User *u)
{
    return u->age >= 18;
}

static Name user_name_typed(const User *u)
{
    Name n = { u->name };
    return n;
}

static bool long_name_typed(const Name *n)
{
    return strlen(n->text) >= 4;
}

static int name_length_typed(const Name *n)
{
    return (int)strlen(n->text);
}

static void print_int_typed(const int *n)
{
    printf("%d\n", *n);
}

STREAM_DEFINE_PREDICATE(adult, User, adult_typed)
STREAM_DEFINE_MAPPER(user_name, User, Name, user_name_typed)
STREAM_DEFINE_PREDICATE(long_name, Name, long_name_typed)
STREAM_DEFINE_MAPPER(name_length, Name, int, name_length_typed)
STREAM_DEFINE_CONSUMER(print_int, int, print_int_typed)

int main(void)
{
    stream_t stream;
    stream_t *s = &stream;

    if (STREAM_OF(
            s,
            User,
            (User){"Alice", 16},
            (User){"Bob", 22},
            (User){"Carol", 35},
            (User){"Dave", 28},
            (User){"Eve", 31}) != STREAM_OK) {
        return 1;
    }

    STREAM_FILTER_TYPED(s, adult)
        ->map(s, sizeof(Name), user_name)
        ->take(s, 3)
        ->filter(s, long_name)
        ->map(s, sizeof(int), name_length)
        ->for_each(s, print_int);

    return s->error == STREAM_ERR_NONE ? 0 : 1;
}
