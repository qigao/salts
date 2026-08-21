#include <turbo/container.h>
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

typedef struct counted_value {
    int *value;
} counted_value;

static size_t copies;
static size_t moves;
static size_t destroys;
static size_t fail_copy_on;

static bool counted_equal(const void *left_, const void *right_) {
    const counted_value *left = (const counted_value *)left_;
    const counted_value *right = (const counted_value *)right_;
    return *left->value == *right->value;
}

static uint64_t counted_hash(const void *value_) {
    const counted_value *value = (const counted_value *)value_;
    return (uint64_t)(unsigned int)*value->value + UINT64_C(1);
}

static bool counted_copy(void *destination_, const void *source_) {
    counted_value *destination = (counted_value *)destination_;
    const counted_value *source = (const counted_value *)source_;

    if (fail_copy_on != 0u && copies + 1u == fail_copy_on)
        return false;
    destination->value = (int *)malloc(sizeof(*destination->value));
    if (destination->value == NULL)
        return false;
    *destination->value = *source->value;
    ++copies;
    return true;
}

static void counted_move(void *destination_, void *source_) {
    counted_value *destination = (counted_value *)destination_;
    counted_value *source = (counted_value *)source_;
    destination->value = source->value;
    source->value = NULL;
    ++moves;
}

static void counted_destroy(void *value_) {
    counted_value *value = (counted_value *)value_;
    free(value->value);
    value->value = NULL;
    ++destroys;
}

static const cmeta_type_traits counted_traits = {
    CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH | CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
        CMETA_TRAIT_DESTROY,
    counted_equal, counted_hash, NULL, counted_copy, counted_move, counted_destroy
};

static const cmeta_type_desc counted_type = {
    "counted", sizeof(counted_value), _Alignof(counted_value), CMETA_T_OBJECT,
    NULL, &counted_traits
};

static const cmeta_type_desc no_hash_type = {
    "no_hash", sizeof(counted_value), _Alignof(counted_value), CMETA_T_OBJECT,
    NULL, NULL
};

static counted_value counted_make(int value) {
    counted_value result;
    result.value = (int *)malloc(sizeof(*result.value));
    *result.value = value;
    return result;
}

static void reset_counts(void) {
    copies = 0u;
    moves = 0u;
    destroys = 0u;
    fail_copy_on = 0u;
}

suite("Container hash ownership") {
    it("requires explicit raw callbacks and typed key traits") {
        turbo_hash_map_t map = {0};

        check_equal(turbo_hash_map_init_bytes(&map, sizeof(int), _Alignof(int),
                                              sizeof(int), _Alignof(int), 1u,
                                              NULL, turbo_hash_key_equal, NULL),
                    CONTAINER_INVALID_ARGUMENT);
        check_equal(turbo_hash_map_init(&map, &no_hash_type, &counted_type, 1u),
                    CONTAINER_TRAIT_MISSING);
        check_equal(turbo_hash_map_init_bytes(&map, sizeof(int), 64u,
                                              sizeof(int), 64u, 0u,
                                              turbo_hash_bytes, turbo_hash_key_equal, NULL),
                    CONTAINER_OK);
        {
            const int key = 1;
            const int value = 1;
            uint64_t generation = turbo_hash_map_generation(&map);
            check_equal(turbo_hash_map_put(&map, &key, &value), CONTAINER_CAPACITY_EXCEEDED);
            check_equal(turbo_hash_map_generation(&map), generation);
        }
        turbo_hash_map_destroy(&map);
    }

    it("enforces live entry limits and honors raw alignment") {
        turbo_hash_map_t map = {0};
        const int one = 1;
        const int two = 2;
        const int value = 9;
        const void *stored_key;
        size_t slot;
        uint64_t generation;

        check_equal(turbo_hash_map_init_bytes(&map, sizeof(one), 64u, sizeof(value), 64u, 1u,
                                              turbo_hash_bytes, turbo_hash_key_equal, NULL),
                    CONTAINER_OK);
        check_equal(turbo_hash_map_reserve(&map, 2u), CONTAINER_CAPACITY_EXCEEDED);
        check_equal(turbo_hash_map_put(&map, &one, &value), CONTAINER_OK);
        stored_key = NULL;
        for (slot = 0u; slot < turbo_hash_map_capacity(&map); ++slot) {
            stored_key = turbo_hash_map_key_at(&map, slot);
            if (stored_key != NULL) break;
        }
        check_not_null(stored_key);
        check_equal((uintptr_t)stored_key % 64u, 0u);
        check_equal((uintptr_t)turbo_hash_map_get(&map, &one) % 64u, 0u);
        generation = map.generation;
        check_equal(turbo_hash_map_put(&map, &two, &value), CONTAINER_CAPACITY_EXCEEDED);
        check_equal(map.generation, generation);
        turbo_hash_map_destroy(&map);
    }

    it("keeps replacement transactional including self aliases") {
        turbo_hash_map_t map = {0};
        counted_value key;
        counted_value first;
        counted_value second;
        counted_value *stored;
        uint64_t generation;

        reset_counts();
        key = counted_make(1);
        first = counted_make(7);
        second = counted_make(8);
        check_equal(turbo_hash_map_init(&map, &counted_type, &counted_type, 2u), CONTAINER_OK);
        check_equal(turbo_hash_map_put(&map, &key, &first), CONTAINER_OK);
        generation = map.generation;
        fail_copy_on = copies + 1u;
        check_equal(turbo_hash_map_put(&map, &key, &second), CONTAINER_OUT_OF_MEMORY);
        stored = (counted_value *)turbo_hash_map_get(&map, &key);
        check_equal(*stored->value, 7);
        check_equal(map.generation, generation);
        fail_copy_on = 0u;
        check_equal(turbo_hash_map_put(&map, &key, stored), CONTAINER_OK);
        check_equal(*((counted_value *)turbo_hash_map_get(&map, &key))->value, 7);
        turbo_hash_map_destroy(&map);
        counted_destroy(&key);
        counted_destroy(&first);
        counted_destroy(&second);
    }

    it("moves removals without writing output for a miss and balances clear destroy") {
        turbo_hash_map_t map = {0};
        counted_value key;
        counted_value value;
        counted_value out = {0};
        counted_value unchanged = {0};
        counted_value missing;

        reset_counts();
        key = counted_make(4);
        value = counted_make(5);
        missing = counted_make(6);
        unchanged = counted_make(77);
        check_equal(turbo_hash_map_init(&map, &counted_type, &counted_type, 2u), CONTAINER_OK);
        check_equal(turbo_hash_map_put(&map, &key, &value), CONTAINER_OK);
        check_equal(turbo_hash_map_remove(&map, &missing, &unchanged), CONTAINER_NOT_FOUND);
        check_equal(*unchanged.value, 77);
        check_equal(turbo_hash_map_remove(&map, &key, &out), CONTAINER_OK);
        check_equal(*out.value, 5);
        counted_destroy(&out);
        check_equal(turbo_hash_map_put(&map, &key, &value), CONTAINER_OK);
        turbo_hash_map_clear(&map);
        turbo_hash_map_destroy(&map);
        counted_destroy(&key);
        counted_destroy(&value);
        counted_destroy(&missing);
        counted_destroy(&unchanged);
    }

    it("keeps failed from arrays unchanged and makes duplicate set add a no-op") {
        turbo_hash_map_t map = {0};
        turbo_hash_map_t before;
        turbo_set_t set = {0};
        const int keys[] = {1, 2};
        const int values[] = {3, 4};
        uint64_t generation;

        check_equal(turbo_hash_map_init_bytes(&map, sizeof(int), _Alignof(int), sizeof(int),
                                              _Alignof(int), 1u, turbo_hash_bytes,
                                              turbo_hash_key_equal, NULL), CONTAINER_OK);
        turbo_hash_map_destroy(&map);
        before = map;
        check_equal(turbo_hash_map_from_arrays_bytes(&map, keys, values, 2u, sizeof(int),
                                                     _Alignof(int), sizeof(int), _Alignof(int),
                                                     1u, turbo_hash_bytes,
                                                     turbo_hash_key_equal, NULL),
                    CONTAINER_CAPACITY_EXCEEDED);
        check_equal(memcmp(&map, &before, sizeof(map)), 0);
        check_equal(turbo_set_init_bytes(&set, sizeof(int), _Alignof(int), 2u,
                                         turbo_hash_bytes, turbo_hash_key_equal, NULL),
                    CONTAINER_OK);
        check_equal(turbo_set_add(&set, &keys[0]), CONTAINER_OK);
        generation = set.map.generation;
        check_equal(turbo_set_add(&set, &keys[0]), CONTAINER_OK);
        check_equal(turbo_set_size(&set), (size_t)1u);
        check_equal(set.map.generation, generation);
        turbo_set_destroy(&set);
    }

    it("commits typed from arrays only after every copy succeeds") {
        turbo_hash_map_t map = {0};
        turbo_hash_map_t before;
        counted_value keys[2];
        counted_value values[2];

        reset_counts();
        keys[0] = counted_make(1);
        keys[1] = counted_make(2);
        values[0] = counted_make(3);
        values[1] = counted_make(4);
        check_equal(turbo_hash_map_init(&map, &counted_type, &counted_type, 2u), CONTAINER_OK);
        turbo_hash_map_destroy(&map);
        before = map;
        fail_copy_on = 3u;
        check_equal(turbo_hash_map_from_arrays(&map, keys, values, 2u, &counted_type,
                                               &counted_type, 2u), CONTAINER_OUT_OF_MEMORY);
        check_equal(memcmp(&map, &before, sizeof(map)), 0);
        counted_destroy(&keys[0]);
        counted_destroy(&keys[1]);
        counted_destroy(&values[0]);
        counted_destroy(&values[1]);
    }

    it("keeps lifecycle generations transactional around reserve and tombstones") {
        turbo_hash_map_t map = {0};
        turbo_hash_map_t before;
        const int one = 1;
        const int two = 2;
        const int value = 7;
        uint64_t generation;

        check_equal(turbo_hash_map_init_bytes(&map, sizeof(int), _Alignof(int), sizeof(int),
                                              _Alignof(int), 2u, turbo_hash_bytes,
                                              turbo_hash_key_equal, NULL), CONTAINER_OK);
        before = map;
        check_equal(turbo_hash_map_init_bytes(&map, sizeof(int), _Alignof(int), sizeof(int),
                                              _Alignof(int), 2u, turbo_hash_bytes,
                                              turbo_hash_key_equal, NULL), CONTAINER_INVALID_ARGUMENT);
        check_equal(memcmp(&map, &before, sizeof(map)), 0);
        check_equal(turbo_hash_map_put(&map, &one, &value), CONTAINER_OK);
        check_equal(turbo_hash_map_remove(&map, &one, NULL), CONTAINER_OK);
        generation = map.generation;
        check_equal(turbo_hash_map_reserve(&map, 1u), CONTAINER_OK);
        check_true(map.generation > generation);
        turbo_hash_map_destroy(&map);
        generation = map.generation;
        check_equal(turbo_hash_map_init_bytes(&map, sizeof(int), _Alignof(int), sizeof(int),
                                              _Alignof(int), 2u, turbo_hash_bytes,
                                              turbo_hash_key_equal, NULL), CONTAINER_OK);
        check_equal(map.generation, generation + UINT64_C(1));
        check_equal(turbo_hash_map_put(&map, &two, &value), CONTAINER_OK);
        turbo_hash_map_destroy(&map);
    }

    it("copies an aliased value before an insertion can rehash storage") {
        turbo_hash_map_t map = {0};
        int key;
        const int source_key = 1;
        const int alias_key = 99;
        const int value = 41;
        const int *aliased_value;

        check_equal(turbo_hash_map_init_bytes(&map, sizeof(int), _Alignof(int), sizeof(int),
                                              _Alignof(int), 12u, turbo_hash_bytes,
                                              turbo_hash_key_equal, NULL), CONTAINER_OK);
        for (key = 1; key <= 11; ++key)
            check_equal(turbo_hash_map_put(&map, &key, &value), CONTAINER_OK);
        aliased_value = (const int *)turbo_hash_map_get_const(&map, &source_key);
        check_not_null(aliased_value);
        check_equal(turbo_hash_map_put(&map, &alias_key, aliased_value), CONTAINER_OK);
        check_equal(*(const int *)turbo_hash_map_get_const(&map, &source_key), 41);
        check_equal(*(const int *)turbo_hash_map_get_const(&map, &alias_key), 41);
        turbo_hash_map_destroy(&map);
    }

    it("advances destroy generation exactly once for empty and live handles") {
        turbo_hash_map_t map = {0};
        turbo_set_t set = {0};
        const int key = 1;
        const int value = 2;

        check_equal(turbo_hash_map_init_bytes(&map, sizeof(int), _Alignof(int), sizeof(int),
                                              _Alignof(int), 1u, turbo_hash_bytes,
                                              turbo_hash_key_equal, NULL), CONTAINER_OK);
        check_equal(turbo_hash_map_generation(&map), UINT64_C(1));
        turbo_hash_map_destroy(&map);
        check_equal(turbo_hash_map_generation(&map), UINT64_C(2));
        turbo_hash_map_destroy(&map);
        check_equal(turbo_hash_map_generation(&map), UINT64_C(2));
        check_equal(turbo_hash_map_init_bytes(&map, sizeof(int), _Alignof(int), sizeof(int),
                                              _Alignof(int), 1u, turbo_hash_bytes,
                                              turbo_hash_key_equal, NULL), CONTAINER_OK);
        check_equal(turbo_hash_map_put(&map, &key, &value), CONTAINER_OK);
        check_equal(turbo_hash_map_generation(&map), UINT64_C(4));
        turbo_hash_map_destroy(&map);
        check_equal(turbo_hash_map_generation(&map), UINT64_C(5));

        check_equal(turbo_set_init_bytes(&set, sizeof(int), _Alignof(int), 1u,
                                         turbo_hash_bytes, turbo_hash_key_equal, NULL),
                    CONTAINER_OK);
        turbo_set_destroy(&set);
        check_equal(turbo_set_generation(&set), UINT64_C(2));
        check_equal(turbo_set_init_bytes(&set, sizeof(int), _Alignof(int), 1u,
                                         turbo_hash_bytes, turbo_hash_key_equal, NULL),
                    CONTAINER_OK);
        check_equal(turbo_set_add(&set, &key), CONTAINER_OK);
        turbo_set_destroy(&set);
        check_equal(turbo_set_generation(&set), UINT64_C(5));
    }

    it("keeps multimap vector ownership behind a pointer carrier") {
        turbo_multimap_t map = {0};
        const int one = 1;
        const int two = 2;
        const int value = 7;
        int out = 0;
        turbo_vec_t *values;

        check_equal(turbo_multimap_init_bytes(&map, sizeof(int), _Alignof(int), 1u,
                                        sizeof(int), _Alignof(int), 1u,
                                        turbo_hash_bytes, turbo_hash_key_equal, NULL), CONTAINER_OK);
        check_equal(turbo_multimap_put(&map, &one, &value), CONTAINER_OK);
        values = turbo_multimap_get_values(&map, &one);
        check_not_null(values);
        check_equal(turbo_vec_size(values), (size_t)1u);
        check_equal(turbo_multimap_put(&map, &one, &value), CONTAINER_CAPACITY_EXCEEDED);
        check_equal(turbo_multimap_put(&map, &two, &value), CONTAINER_CAPACITY_EXCEEDED);
        check_true(turbo_multimap_remove(&map, &one, &out));
        check_equal(out, 7);
        check_true(turbo_multimap_empty(&map));
        turbo_multimap_destroy(&map);
    }

    it("counts typed set ownership and accepts duplicate arrays by live entry limit") {
        turbo_set_t set = {0};
        turbo_hash_map_t map = {0};
        counted_value key;
        const int keys[] = {1, 1};
        const int values[] = {3, 4};
        uint64_t generation;

        reset_counts();
        key = counted_make(1);
        check_equal(turbo_set_init(&set, &counted_type, 1u), CONTAINER_OK);
        check_equal(turbo_set_add(&set, &key), CONTAINER_OK);
        generation = turbo_set_generation(&set);
        check_equal(copies, (size_t)1u);
        check_equal(turbo_set_add(&set, &key), CONTAINER_OK);
        check_equal(copies, (size_t)1u);
        check_equal(turbo_set_generation(&set), generation);
        turbo_set_destroy(&set);
        counted_destroy(&key);
        check_equal(destroys, (size_t)3u);

        check_equal(turbo_hash_map_from_arrays_bytes(&map, keys, values, 2u, sizeof(int),
                                                     _Alignof(int), sizeof(int), _Alignof(int),
                                                     1u, turbo_hash_bytes,
                                                     turbo_hash_key_equal, NULL), CONTAINER_OK);
        check_equal(turbo_hash_map_size(&map), (size_t)1u);
        check_equal(*(const int *)turbo_hash_map_get_const(&map, &keys[0]), 4);
        check_equal(turbo_hash_map_entry_limit(&map), (size_t)1u);
        turbo_hash_map_destroy(&map);
    }

    it("rejects reserve metadata overflow without changing a SIZE_MAX-limited map") {
        turbo_hash_map_t map = {0};
        turbo_hash_map_t before;
        const int key = 1;
        const int value = 1;

        check_equal(turbo_hash_map_init_bytes(&map, sizeof(int), _Alignof(int), sizeof(int),
                                              _Alignof(int), SIZE_MAX, turbo_hash_bytes,
                                              turbo_hash_key_equal, NULL), CONTAINER_OK);
        check_equal(turbo_hash_map_put(&map, &key, &value), CONTAINER_OK);
        before = map;
        check_equal(turbo_hash_map_reserve(&map, SIZE_MAX), CONTAINER_CAPACITY_EXCEEDED);
        check_equal(memcmp(&map, &before, sizeof(map)), 0);
        turbo_hash_map_destroy(&map);
    }

    it("balances typed clear and NULL removal ownership") {
        turbo_hash_map_t map = {0};
        counted_value keys[2];
        counted_value values[2];

        reset_counts();
        keys[0] = counted_make(1); keys[1] = counted_make(2);
        values[0] = counted_make(3); values[1] = counted_make(4);
        check_equal(turbo_hash_map_init(&map, &counted_type, &counted_type, 2u), CONTAINER_OK);
        check_equal(turbo_hash_map_put(&map, &keys[0], &values[0]), CONTAINER_OK);
        check_equal(turbo_hash_map_put(&map, &keys[1], &values[1]), CONTAINER_OK);
        check_equal(copies, (size_t)4u);
        check_equal(moves, (size_t)4u);
        turbo_hash_map_clear(&map);
        check_equal(destroys, (size_t)8u);
        turbo_hash_map_destroy(&map);
        counted_destroy(&keys[0]); counted_destroy(&keys[1]);
        counted_destroy(&values[0]); counted_destroy(&values[1]);
        check_equal(destroys, (size_t)12u);

        reset_counts();
        keys[0] = counted_make(5); values[0] = counted_make(6);
        check_equal(turbo_hash_map_init(&map, &counted_type, &counted_type, 1u), CONTAINER_OK);
        check_equal(turbo_hash_map_put(&map, &keys[0], &values[0]), CONTAINER_OK);
        check_equal(turbo_hash_map_remove(&map, &keys[0], NULL), CONTAINER_OK);
        check_equal(destroys, (size_t)4u);
        turbo_hash_map_destroy(&map);
        counted_destroy(&keys[0]); counted_destroy(&values[0]);
        check_equal(destroys, (size_t)6u);
    }

    it("moves and destroys typed entries during rehash") {
        turbo_hash_map_t map = {0};
        counted_value keys[12];
        counted_value values[12];
        size_t index;

        reset_counts();
        for (index = 0u; index < 12u; ++index) {
            keys[index] = counted_make((int)index + 1);
            values[index] = counted_make((int)index + 101);
        }
        check_equal(turbo_hash_map_init(&map, &counted_type, &counted_type, 12u), CONTAINER_OK);
        for (index = 0u; index < 12u; ++index)
            check_equal(turbo_hash_map_put(&map, &keys[index], &values[index]), CONTAINER_OK);
        check_equal(copies, (size_t)24u);
        check_equal(moves, (size_t)46u);
        check_equal(destroys, (size_t)46u);
        turbo_hash_map_destroy(&map);
        check_equal(destroys, (size_t)70u);
        for (index = 0u; index < 12u; ++index) {
            counted_destroy(&keys[index]);
            counted_destroy(&values[index]);
        }
        check_equal(destroys, (size_t)94u);
    }
}
