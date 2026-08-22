#include <cmeta/entry.h>
#include "tinytest.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct tracked_value {
    int *value;
} tracked_value;

static size_t tracked_live;
static size_t tracked_copy_calls;
static size_t tracked_move_calls;
static size_t tracked_destroy_calls;
static size_t tracked_fail_copy_at;

static tracked_value tracked_make(int value) {
    tracked_value result = {0};
    result.value = (int *)malloc(sizeof(*result.value));
    if (result.value != NULL) {
        *result.value = value;
        ++tracked_live;
    }
    return result;
}

static bool tracked_equal(const void *left_, const void *right_) {
    const tracked_value *left = (const tracked_value *)left_;
    const tracked_value *right = (const tracked_value *)right_;
    return left != NULL && right != NULL && left->value != NULL &&
           right->value != NULL && *left->value == *right->value;
}

static uint64_t tracked_hash(const void *value_) {
    const tracked_value *value = (const tracked_value *)value_;
    return value != NULL && value->value != NULL
               ? (uint64_t)(uint32_t)*value->value * UINT64_C(0x9e3779b1)
               : UINT64_C(0);
}

static int tracked_compare(const void *left_, const void *right_) {
    const tracked_value *left = (const tracked_value *)left_;
    const tracked_value *right = (const tracked_value *)right_;
    if (left == NULL || right == NULL || left->value == NULL ||
        right->value == NULL)
        return 0;
    return (*left->value > *right->value) - (*left->value < *right->value);
}

static bool tracked_copy(void *destination_, const void *source_) {
    tracked_value *destination = (tracked_value *)destination_;
    const tracked_value *source = (const tracked_value *)source_;

    ++tracked_copy_calls;
    if (tracked_fail_copy_at != 0u &&
        tracked_copy_calls == tracked_fail_copy_at)
        return false;
    if (destination == NULL || source == NULL || source->value == NULL)
        return false;
    *destination = tracked_make(*source->value);
    return destination->value != NULL;
}

static void tracked_move(void *destination_, void *source_) {
    tracked_value *destination = (tracked_value *)destination_;
    tracked_value *source = (tracked_value *)source_;
    if (destination == NULL || source == NULL)
        return;
    destination->value = source->value;
    source->value = NULL;
    ++tracked_move_calls;
}

static void tracked_destroy(void *value_) {
    tracked_value *value = (tracked_value *)value_;
    if (value != NULL && value->value != NULL) {
        free(value->value);
        value->value = NULL;
        --tracked_live;
    }
    ++tracked_destroy_calls;
}

static const cmeta_type_traits tracked_traits = {
    .flags = CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH | CMETA_TRAIT_COMPARE |
             CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .equal = tracked_equal,
    .hash = tracked_hash,
    .compare = tracked_compare,
    .copy_construct = tracked_copy,
    .move_construct = tracked_move,
    .destroy = tracked_destroy
};

static const cmeta_type_desc tracked_type = {
    .name = "tracked_value",
    .size = sizeof(tracked_value),
    .align = 64u,
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &tracked_traits,
    .identity = NULL
};

static const cmeta_type_desc different_tracked_type = {
    .name = "different_tracked_value",
    .size = sizeof(tracked_value),
    .align = 64u,
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &tracked_traits,
    .identity = NULL
};

static void reset_counters(void) {
    tracked_copy_calls = 0u;
    tracked_move_calls = 0u;
    tracked_destroy_calls = 0u;
    tracked_fail_copy_at = 0u;
}

spec("CMeta associative entry") {
    it("copies a borrowed entry into independently owned aligned storage") {
        tracked_value key = tracked_make(1);
        tracked_value value = tracked_make(10);
        cmeta_entry borrowed = {
            .key_type = &tracked_type,
            .value_type = &tracked_type,
            .key = &key,
            .value = &value,
            .key_storage = NULL,
            .value_storage = NULL
        };
        cmeta_entry owned = {0};

        reset_counters();
        check_true(cmeta_type_hash_entry.traits->copy_construct(&owned,
                                                                 &borrowed));
        check_true(owned.key_storage != NULL);
        check_true(owned.value_storage != NULL);
        check_true(owned.key != borrowed.key);
        check_true(owned.value != borrowed.value);
        check_equal((uintptr_t)owned.key % tracked_type.align, (uintptr_t)0u);
        check_equal((uintptr_t)owned.value % tracked_type.align,
                    (uintptr_t)0u);
        check_equal(tracked_copy_calls, (size_t)2u);

        cmeta_type_hash_entry.traits->destroy(&owned);
        tracked_destroy(&value);
        tracked_destroy(&key);
        check_equal(tracked_live, (size_t)0u);
    }

    it("rolls back copied key when value copy construction fails") {
        tracked_value key = tracked_make(2);
        tracked_value value = tracked_make(20);
        cmeta_entry borrowed = {
            .key_type = &tracked_type,
            .value_type = &tracked_type,
            .key = &key,
            .value = &value,
            .key_storage = NULL,
            .value_storage = NULL
        };
        cmeta_entry owned = {0};
        size_t live_before = tracked_live;

        reset_counters();
        tracked_fail_copy_at = 2u;
        check_false(cmeta_type_hash_entry.traits->copy_construct(&owned,
                                                                  &borrowed));
        check_null(owned.key);
        check_null(owned.value);
        check_null(owned.key_storage);
        check_null(owned.value_storage);
        check_equal(tracked_live, live_before);
        check_equal(tracked_copy_calls, (size_t)2u);
        check_equal(tracked_destroy_calls, (size_t)1u);

        tracked_fail_copy_at = 0u;
        tracked_destroy(&value);
        tracked_destroy(&key);
        check_equal(tracked_live, (size_t)0u);
    }

    it("moves borrowed wrapper state without invoking child move callbacks") {
        tracked_value key = tracked_make(3);
        tracked_value value = tracked_make(30);
        cmeta_entry borrowed = {
            .key_type = &tracked_type,
            .value_type = &tracked_type,
            .key = &key,
            .value = &value,
            .key_storage = NULL,
            .value_storage = NULL
        };
        cmeta_entry moved = {0};
        size_t live_before = tracked_live;

        reset_counters();
        cmeta_type_ordered_entry.traits->move_construct(&moved, &borrowed);
        check_equal(tracked_move_calls, (size_t)0u);
        check_null(borrowed.key);
        check_null(borrowed.value);
        check_null(borrowed.key_type);
        check_null(borrowed.value_type);
        check_true(moved.key == &key);
        check_true(moved.value == &value);
        cmeta_type_ordered_entry.traits->destroy(&moved);
        check_equal(tracked_live, live_before);

        tracked_destroy(&value);
        tracked_destroy(&key);
        check_equal(tracked_live, (size_t)0u);
    }

    it("moves owned storage exactly once without invoking child move callbacks") {
        tracked_value key = tracked_make(4);
        tracked_value value = tracked_make(40);
        cmeta_entry borrowed = {
            .key_type = &tracked_type,
            .value_type = &tracked_type,
            .key = &key,
            .value = &value,
            .key_storage = NULL,
            .value_storage = NULL
        };
        cmeta_entry owned = {0};
        cmeta_entry moved = {0};

        reset_counters();
        check_true(cmeta_type_ordered_entry.traits->copy_construct(&owned,
                                                                    &borrowed));
        tracked_move_calls = 0u;
        cmeta_type_ordered_entry.traits->move_construct(&moved, &owned);
        check_equal(tracked_move_calls, (size_t)0u);
        check_null(owned.key_storage);
        check_null(owned.value_storage);
        check_true(moved.key_storage != NULL);
        check_true(moved.value_storage != NULL);
        cmeta_type_ordered_entry.traits->destroy(&owned);
        cmeta_type_ordered_entry.traits->destroy(&moved);

        tracked_destroy(&value);
        tracked_destroy(&key);
        check_equal(tracked_live, (size_t)0u);
    }

    it("advertises only truthful hash and ordered capabilities") {
        cmeta_trait_flags hash_flags = cmeta_type_hash_entry.traits->flags;
        cmeta_trait_flags ordered_flags = cmeta_type_ordered_entry.traits->flags;

        check_true((hash_flags & (CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                                  CMETA_TRAIT_DESTROY | CMETA_TRAIT_EQUAL |
                                  CMETA_TRAIT_HASH)) ==
                   (CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                    CMETA_TRAIT_DESTROY | CMETA_TRAIT_EQUAL |
                    CMETA_TRAIT_HASH));
        check_equal(hash_flags & CMETA_TRAIT_COMPARE, (cmeta_trait_flags)0u);
        check_true((ordered_flags & (CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                                     CMETA_TRAIT_DESTROY |
                                     CMETA_TRAIT_COMPARE)) ==
                   (CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                    CMETA_TRAIT_DESTROY | CMETA_TRAIT_COMPARE));
        check_equal(ordered_flags & (CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH),
                    (cmeta_trait_flags)0u);
    }

    it("uses key semantics and rejects mismatched key bindings safely") {
        tracked_value key_a = tracked_make(5);
        tracked_value key_b = tracked_make(5);
        tracked_value key_c = tracked_make(6);
        tracked_value value = tracked_make(50);
        cmeta_entry a = {
            .key_type = &tracked_type,
            .value_type = &tracked_type,
            .key = &key_a,
            .value = &value
        };
        cmeta_entry b = {
            .key_type = &tracked_type,
            .value_type = &tracked_type,
            .key = &key_b,
            .value = &value
        };
        cmeta_entry c = {
            .key_type = &tracked_type,
            .value_type = &tracked_type,
            .key = &key_c,
            .value = &value
        };
        cmeta_entry mismatch = {
            .key_type = &different_tracked_type,
            .value_type = &tracked_type,
            .key = &key_b,
            .value = &value
        };

        check_true(cmeta_type_hash_entry.traits->equal(&a, &b));
        check_false(cmeta_type_hash_entry.traits->equal(&a, &c));
        check_equal(cmeta_type_hash_entry.traits->hash(&a),
                    cmeta_type_hash_entry.traits->hash(&b));
        check_true(cmeta_type_ordered_entry.traits->compare(&a, &c) < 0);
        check_false(cmeta_type_hash_entry.traits->equal(&a, &mismatch));
        check_equal(cmeta_type_ordered_entry.traits->compare(&a, &mismatch),
                    0);

        tracked_destroy(&value);
        tracked_destroy(&key_c);
        tracked_destroy(&key_b);
        tracked_destroy(&key_a);
        check_equal(tracked_live, (size_t)0u);
    }
}
