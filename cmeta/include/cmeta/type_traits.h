#ifndef CMETA_TYPE_TRAITS_H
#define CMETA_TYPE_TRAITS_H

#include <cmeta/pp.h>

#include <stdbool.h>
#include <float.h>
#include <limits.h>
#include <stdint.h>

typedef uint32_t cmeta_trait_flags;

enum {
    CMETA_TRAIT_EQUAL = 1u << 0,
    CMETA_TRAIT_HASH = 1u << 1,
    CMETA_TRAIT_COMPARE = 1u << 2,
    CMETA_TRAIT_COPY = 1u << 3,
    CMETA_TRAIT_MOVE = 1u << 4,
    CMETA_TRAIT_DESTROY = 1u << 5,
    CMETA_TRAIT_TRIVIAL_COPY = 1u << 6,
    CMETA_TRAIT_TRIVIAL_DESTROY = 1u << 7,
    CMETA_TRAIT_MASK = CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH |
                       CMETA_TRAIT_COMPARE | CMETA_TRAIT_COPY |
                       CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY |
                       CMETA_TRAIT_TRIVIAL_COPY |
                       CMETA_TRAIT_TRIVIAL_DESTROY
};

typedef struct cmeta_type_traits {
    cmeta_trait_flags flags;
    bool (*equal)(const void *, const void *);
    uint64_t (*hash)(const void *);
    int (*compare)(const void *, const void *);
    bool (*copy_construct)(void *, const void *);
    void (*move_construct)(void *, void *);
    void (*destroy)(void *);
} cmeta_type_traits;

/* Floating-key semantics: +0 and -0 compare equal and hash equally; every
 * NaN encoding compares equal to every other NaN, sorts after finite/infinite
 * values, and hashes to one canonical value.
 *
 * Their raw-object hash implementation is deliberately fail-fast: CMeta only
 * supports IEC 60559 binary32/binary64 object representations without padding.
 * The expressions below record the checked <float.h>, byte, and copy-width
 * requirements; `CMETA_FLOAT_TRAITS_BINARY32_BINARY64` includes all of them.
 * cmeta.c rejects compilation when they are not met and supplies no fallback. */
#define CMETA_FLOAT_TRAITS_OBJECT_HASH_WIDTHS \
    (CHAR_BIT == 8 && sizeof(uint32_t) == sizeof(float) && \
     sizeof(uint64_t) == sizeof(double))

#define CMETA_FLOAT_TRAITS_BINARY32_BINARY64 \
    (CMETA_FLOAT_TRAITS_OBJECT_HASH_WIDTHS && \
     FLT_RADIX == 2 && sizeof(float) == 4u && sizeof(double) == 8u && \
     FLT_MANT_DIG == 24 && DBL_MANT_DIG == 53 && \
     FLT_MIN_EXP == -125 && FLT_MAX_EXP == 128 && \
     DBL_MIN_EXP == -1021 && DBL_MAX_EXP == 1024)

#ifdef __cplusplus
extern "C" {
#endif
extern const cmeta_type_traits cmeta_traits_bool;
extern const cmeta_type_traits cmeta_traits_int;
extern const cmeta_type_traits cmeta_traits_long;
extern const cmeta_type_traits cmeta_traits_float;
extern const cmeta_type_traits cmeta_traits_double;
#ifdef __cplusplus
}
#endif

/* Internal compatibility constructor for the normalized positional payload. */
#define CMETA_TRAITS_POSITIONAL(name, flags_, equal_, hash_, compare_, copy_, move_, destroy_) \
    CMETA_LOCAL const cmeta_type_traits cmeta_traits_##name = { \
        (flags_), (equal_), (hash_), (compare_), (copy_), (move_), (destroy_) \
    }

#define CMETA_TRAIT_FLAG_equal   CMETA_TRAIT_EQUAL
#define CMETA_TRAIT_FLAG_hash    CMETA_TRAIT_HASH
#define CMETA_TRAIT_FLAG_compare CMETA_TRAIT_COMPARE
#define CMETA_TRAIT_FLAG_copy    CMETA_TRAIT_COPY
#define CMETA_TRAIT_FLAG_move    CMETA_TRAIT_MOVE
#define CMETA_TRAIT_FLAG_destroy CMETA_TRAIT_DESTROY

#define CMETA_TRAIT_INIT_equal(fn)   .equal = (fn),
#define CMETA_TRAIT_INIT_hash(fn)    .hash = (fn),
#define CMETA_TRAIT_INIT_compare(fn) .compare = (fn),
#define CMETA_TRAIT_INIT_copy(fn)    .copy_construct = (fn),
#define CMETA_TRAIT_INIT_move(fn)    .move_construct = (fn),
#define CMETA_TRAIT_INIT_destroy(fn) .destroy = (fn),

#define CMETA_TRAIT_FLAG_ROW(tag, fn) \
    | CMETA_PP_CAT(CMETA_TRAIT_FLAG_, tag)
#define CMETA_TRAIT_INIT_ROW(tag, fn) \
    CMETA_PP_CAT(CMETA_TRAIT_INIT_, tag)(fn)

/* Public heterogeneous trait schema. Flags and function slots are derived from
 * the same tagged rows, so capability presence is declared exactly once. */
#define Traits(name, ...) \
    CMETA_LOCAL const cmeta_type_traits cmeta_traits_##name = { \
        .flags = (cmeta_trait_flags)(0u Schema(CMETA_TRAIT_FLAG_ROW, __VA_ARGS__)), \
        Schema(CMETA_TRAIT_INIT_ROW, __VA_ARGS__) \
    }

#endif
