#include <cmeta/type_traits.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct sample_point {
    int x;
    int y;
} sample_point;

static bool sample_point_equal(const void *a, const void *b) {
    const sample_point *left = (const sample_point *)a;
    const sample_point *right = (const sample_point *)b;
    return left->x == right->x && left->y == right->y;
}

static uint64_t sample_point_hash(const void *value) {
    const sample_point *point = (const sample_point *)value;
    return (uint64_t)(unsigned)point->x * 131u + (uint64_t)(unsigned)point->y;
}

static bool sample_point_copy(void *dst, const void *src) {
    memcpy(dst, src, sizeof(sample_point));
    return true;
}

Traits(sample_point,
    (equal, sample_point_equal),
    (hash, sample_point_hash),
    (copy, sample_point_copy)
);

int main(void) {
    assert(cmeta_traits_sample_point.flags ==
        (CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH | CMETA_TRAIT_COPY));
    assert(cmeta_traits_sample_point.equal == sample_point_equal);
    assert(cmeta_traits_sample_point.hash == sample_point_hash);
    assert(cmeta_traits_sample_point.copy_construct == sample_point_copy);
    assert(cmeta_traits_sample_point.compare == NULL);
    assert(cmeta_traits_sample_point.move_construct == NULL);
    assert(cmeta_traits_sample_point.destroy == NULL);
    return 0;
}
