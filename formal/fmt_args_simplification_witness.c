#include <fmt.h>

#include <stddef.h>
#include <stdio.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "fmt args simplification check failed: %s\n", #expr); \
        return 1; \
    } \
} while (0)

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* Candidate normal form for every non-empty argument list:
 * map FMT_ARG over the real arguments, then append one unobservable NONE slot.
 * Zero arguments use the same semantic storage normal form directly. */
#define FMT_PROOF_ITEM(arg, ignored) FMT_ARG(arg),
#define FMT_PROOF_NORMALIZED_NONEMPTY(...) \
    (fmt_arg_t[]) { \
        CMETA_PP_FOR_EACH(FMT_PROOF_ITEM, ~, __VA_ARGS__) \
        { FMT_TYPE_NONE } \
    }
#define FMT_PROOF_NORMALIZED_EMPTY() \
    (fmt_arg_t[]) { { FMT_TYPE_NONE } }

static int same_observable_prefix(const fmt_arg_t *legacy,
                                  const fmt_arg_t *normalized,
                                  size_t logical_count) {
    size_t i;
    for (i = 0; i < logical_count; ++i) {
        if (legacy[i].type != normalized[i].type) return 0;
        if (legacy[i].type != FMT_TYPE_INT) return 0;
        if (legacy[i].val.i != normalized[i].val.i) return 0;
    }
    return 1;
}

static int check_case(const fmt_arg_t *legacy,
                      size_t legacy_storage_count,
                      const fmt_arg_t *normalized,
                      size_t normalized_storage_count,
                      size_t logical_count) {
    if (logical_count == 0u) {
        if (legacy_storage_count != 1u) return 0;
        if (legacy[0].type != FMT_TYPE_NONE) return 0;
    } else if (legacy_storage_count != logical_count) {
        return 0;
    }

    if (normalized_storage_count != logical_count + 1u) return 0;
    if (normalized[logical_count].type != FMT_TYPE_NONE) return 0;
    return same_observable_prefix(legacy, normalized, logical_count);
}

#define CHECK_NONEMPTY_CASE(count, ...) do { \
    fmt_arg_t *legacy = FMT_ARGS(__VA_ARGS__); \
    fmt_arg_t *normalized = FMT_PROOF_NORMALIZED_NONEMPTY(__VA_ARGS__); \
    CHECK(FMT_NARGS(__VA_ARGS__) == (count)); \
    CHECK(check_case(legacy, (count), normalized, (count) + 1u, (count))); \
} while (0)

int main(void) {
    fmt_arg_t *legacy_empty = FMT_ARGS();
    fmt_arg_t *normalized_empty = FMT_PROOF_NORMALIZED_EMPTY();

    CHECK(FMT_NARGS() == 0);
    CHECK(check_case(legacy_empty, 1u, normalized_empty, 1u, 0u));

    CHECK_NONEMPTY_CASE(1u, 1);
    CHECK_NONEMPTY_CASE(2u, 1, 2);
    CHECK_NONEMPTY_CASE(3u, 1, 2, 3);
    CHECK_NONEMPTY_CASE(4u, 1, 2, 3, 4);
    CHECK_NONEMPTY_CASE(5u, 1, 2, 3, 4, 5);
    CHECK_NONEMPTY_CASE(6u, 1, 2, 3, 4, 5, 6);
    CHECK_NONEMPTY_CASE(7u, 1, 2, 3, 4, 5, 6, 7);
    CHECK_NONEMPTY_CASE(8u, 1, 2, 3, 4, 5, 6, 7, 8);

    puts("fmt args simplification applicability: ok");
    return 0;
}
