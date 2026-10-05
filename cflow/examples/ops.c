#include "ops.h"

cmeta_function(filter, value, bool, even, (int x)) {
    return (x % 2) == 0;
}

cmeta_function(map, value, long, square, (int x)) {
    return (long)x * (long)x;
}

cmeta_function(map, value, double, half, (long x)) {
    return (double)x / 2.0;
}

/* Canonical flatMap ABI: cursor-aware, resumable, O(1) state. */
cmeta_function(flatMap, value, cmeta_gen_status, expand_long,
      (int x, long *out, size_t *cursor)) {
    if (*cursor == 0) {
        *out = (long)x;
        *cursor = 1;
        return CMETA_GEN_VALUE;
    }
    if (*cursor == 1) {
        *out = (long)x * 10L;
        *cursor = 2;
        return CMETA_GEN_VALUE_AND_DONE;
    }
    return CMETA_GEN_DONE;
}

cmeta_function(flatMap, fallible, cmeta_gen_status, fail_long,
      (int x, long *out, size_t *cursor)) {
    (void)x; (void)out; (void)cursor;
    return CMETA_GEN_ERROR;
}

cmeta_function(reduce, associative, long, add_long, (long a, long b)) {
    return a + b;
}

cmeta_function(map, value, double, as_double, (int x)) {
    return (double)x + 0.25;
}

cmeta_function(map, value, int, to_int, (double x)) {
    return (int)x;
}

cmeta_function(zip, value, double, merge_long_double, (long a, double b)) {
    return (double)a + b;
}

cmeta_function(map, value, long, times_ten, (int x)) {
    return (long)x * 10L;
}

cmeta_function(map, value, long, plus_hundred, (int x)) {
    return (long)x + 100L;
}

cmeta_function(transform, value, long, times_two_transform, (int x)) {
    return (long)x * 2L;
}

/* Effect-tagged examples deliberately use semantic contracts instead of bitsets. */
cmeta_function(map, io, long, io_tagged, (int x)) {
    return (long)x * 3L;
}

cmeta_function(map, fallible, long, may_fail_tagged, (int x)) {
    return (long)x + 7L;
}

cmeta_function(map, idempotent, int, clamp_nonnegative, (int x)) {
    return x < 0 ? 0 : x;
}

cmeta_function(map, value, int, clamp_unproven, (int x)) {
    return x < 0 ? 0 : x;
}

cmeta_function(map, pure, long, unproven_square, (int x)) {
    return (long)x * (long)x;
}
