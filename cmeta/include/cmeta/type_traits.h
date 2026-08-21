#ifndef CMETA_TYPE_TRAITS_H
#define CMETA_TYPE_TRAITS_H

#include <cmeta/pp.h>

#include <stdbool.h>
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
 * values, and hashes to one canonical value. */
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

#define Traits(name, flags_, equal_, hash_, compare_, copy_, move_, destroy_) \
    CMETA_LOCAL const cmeta_type_traits cmeta_traits_##name = { \
        (flags_), (equal_), (hash_), (compare_), (copy_), (move_), (destroy_) \
    }

#endif
