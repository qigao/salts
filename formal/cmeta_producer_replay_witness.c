#include <cmeta/pp.h>

#include <stddef.h>
#include <stdio.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "producer replay check failed: %s\n", #expr); \
        return 1; \
    } \
} while (0)

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* Producer macros represent finite zero-or-more sequences directly.  They do
 * not encode a raw variadic list whose emptiness must first be detected. */
#define PRODUCER_EMPTY(M)
#define PRODUCER_ONE(M) M(11)
#define PRODUCER_THREE(M) M(11) M(22) M(33)

/* Append is producer composition: replay the left producer, then the right. */
#define PRODUCER_LEFT(M) M(11)
#define PRODUCER_RIGHT(M) M(22) M(33)
#define PRODUCER_APPEND(M) PRODUCER_LEFT(M) PRODUCER_RIGHT(M)

/* Independent consumers of the exact same producer source. */
#define PRODUCER_VALUE_ITEM(x) x,
#define PRODUCER_COUNT_ITEM(x) + 1
#define PRODUCER_STORAGE_ITEM(x) x,

enum {
    PRODUCER_COUNT_EMPTY = 0 Replay(PRODUCER_EMPTY, PRODUCER_COUNT_ITEM),
    PRODUCER_COUNT_ONE = 0 Replay(PRODUCER_ONE, PRODUCER_COUNT_ITEM),
    PRODUCER_COUNT_THREE = 0 Replay(PRODUCER_THREE, PRODUCER_COUNT_ITEM),
    PRODUCER_COUNT_APPEND = 0 Replay(PRODUCER_APPEND, PRODUCER_COUNT_ITEM)
};

static const int producer_values_empty[] = {
    Replay(PRODUCER_EMPTY, PRODUCER_VALUE_ITEM)
    -1
};

static const int producer_values_one[] = {
    Replay(PRODUCER_ONE, PRODUCER_VALUE_ITEM)
    -1
};

static const int producer_values_three[] = {
    Replay(PRODUCER_THREE, PRODUCER_VALUE_ITEM)
    -1
};

static const int producer_values_append[] = {
    Replay(PRODUCER_APPEND, PRODUCER_VALUE_ITEM)
    -1
};

static const int producer_storage_empty[] = {
    Replay(PRODUCER_EMPTY, PRODUCER_STORAGE_ITEM)
    -1
};

static const int producer_storage_one[] = {
    Replay(PRODUCER_ONE, PRODUCER_STORAGE_ITEM)
    -1
};

static const int producer_storage_three[] = {
    Replay(PRODUCER_THREE, PRODUCER_STORAGE_ITEM)
    -1
};

int main(void) {
    /* Empty replay is valid strict-C11 syntax and emits no mapped item. */
    CHECK(PRODUCER_COUNT_EMPTY == 0);
    CHECK(ARRAY_COUNT(producer_values_empty) == 1u);
    CHECK(producer_values_empty[0] == -1);

    CHECK(PRODUCER_COUNT_ONE == 1);
    CHECK(ARRAY_COUNT(producer_values_one) == 2u);
    CHECK(producer_values_one[0] == 11);
    CHECK(producer_values_one[1] == -1);

    CHECK(PRODUCER_COUNT_THREE == 3);
    CHECK(ARRAY_COUNT(producer_values_three) == 4u);
    CHECK(producer_values_three[0] == 11);
    CHECK(producer_values_three[1] == 22);
    CHECK(producer_values_three[2] == 33);
    CHECK(producer_values_three[3] == -1);

    /* Producer append preserves order and count without arity dispatch. */
    CHECK(PRODUCER_COUNT_APPEND == 3);
    CHECK(ARRAY_COUNT(producer_values_append) == 4u);
    CHECK(producer_values_append[0] == 11);
    CHECK(producer_values_append[1] == 22);
    CHECK(producer_values_append[2] == 33);
    CHECK(producer_values_append[3] == -1);

    /* Count-by-replay and count-by-normalized-storage agree. */
    CHECK(ARRAY_COUNT(producer_storage_empty) - 1u == (size_t)PRODUCER_COUNT_EMPTY);
    CHECK(ARRAY_COUNT(producer_storage_one) - 1u == (size_t)PRODUCER_COUNT_ONE);
    CHECK(ARRAY_COUNT(producer_storage_three) - 1u == (size_t)PRODUCER_COUNT_THREE);

    puts("cmeta producer replay applicability: ok");
    return 0;
}
