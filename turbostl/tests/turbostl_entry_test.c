#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct owned_entry_value {
    int *value;
} owned_entry_value;

#define CMETA_USER_TYPE_LIST \
    , (O, owned_entry_value, cmeta_type_owned_entry_value, CMETA_T_OBJECT, \
       cmeta_traits_owned_entry_value)

#include <turbo/stl/typed.h>
#include "tinytest.h"

static size_t owned_entry_live;
static size_t owned_entry_copy_calls;
static size_t owned_entry_move_calls;
static size_t owned_entry_destroy_calls;
static size_t owned_entry_fail_copy_at;

static owned_entry_value owned_entry_make(int value) {
    owned_entry_value result = {0};
    result.value = (int *)malloc(sizeof(*result.value));
    if (result.value != NULL) {
        *result.value = value;
        ++owned_entry_live;
    }
    return result;
}

static bool owned_entry_equal(const void *left_, const void *right_) {
    const owned_entry_value *left = (const owned_entry_value *)left_;
    const owned_entry_value *right = (const owned_entry_value *)right_;
    return left != NULL && right != NULL && left->value != NULL &&
           right->value != NULL && *left->value == *right->value;
}

static uint64_t owned_entry_hash(const void *value_) {
    const owned_entry_value *value = (const owned_entry_value *)value_;
    return value != NULL && value->value != NULL
               ? (uint64_t)(uint32_t)*value->value * UINT64_C(0x9e3779b1)
               : UINT64_C(0);
}

static int owned_entry_compare(const void *left_, const void *right_) {
    const owned_entry_value *left = (const owned_entry_value *)left_;
    const owned_entry_value *right = (const owned_entry_value *)right_;
    return (*left->value > *right->value) - (*left->value < *right->value);
}

static bool owned_entry_copy(void *destination_, const void *source_) {
    owned_entry_value *destination = (owned_entry_value *)destination_;
    const owned_entry_value *source = (const owned_entry_value *)source_;
    ++owned_entry_copy_calls;
    if (owned_entry_fail_copy_at != 0u &&
        owned_entry_copy_calls == owned_entry_fail_copy_at)
        return false;
    if (destination == NULL || source == NULL || source->value == NULL)
        return false;
    *destination = owned_entry_make(*source->value);
    return destination->value != NULL;
}

static void owned_entry_move(void *destination_, void *source_) {
    owned_entry_value *destination = (owned_entry_value *)destination_;
    owned_entry_value *source = (owned_entry_value *)source_;
    destination->value = source->value;
    source->value = NULL;
    ++owned_entry_move_calls;
}

static void owned_entry_destroy(void *value_) {
    owned_entry_value *value = (owned_entry_value *)value_;
    if (value != NULL && value->value != NULL) {
        free(value->value);
        value->value = NULL;
        --owned_entry_live;
    }
    ++owned_entry_destroy_calls;
}

const cmeta_type_traits cmeta_traits_owned_entry_value = {
    CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH | CMETA_TRAIT_COMPARE |
        CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    owned_entry_equal, owned_entry_hash, owned_entry_compare,
    owned_entry_copy, owned_entry_move, owned_entry_destroy
};

const cmeta_type_desc cmeta_type_owned_entry_value = {
    "owned_entry_value", sizeof(owned_entry_value),
    _Alignof(owned_entry_value), CMETA_T_OBJECT, NULL,
    &cmeta_traits_owned_entry_value
};

const cmeta_type_desc cmeta_type_owned_entry_value_ptr = {
    "owned_entry_value *", sizeof(owned_entry_value *),
    _Alignof(owned_entry_value *), CMETA_T_POINTER,
    &cmeta_type_owned_entry_value, NULL
};

typed(HashMap, OwnedEntryMap, owned_entry_value, owned_entry_value);
typed(BTree, OwnedEntryTree, owned_entry_value, owned_entry_value);

spec("Container composed entry descriptors") {
    it("copies a borrowed Range entry into an owned collector transient") {
        OwnedEntryMap source = {0};
        OwnedEntryMap output = {0};
        OwnedEntryMap_entry input = {owned_entry_make(1), owned_entry_make(10)};
        OwnedEntryMap_entry borrowed = {0};
        OwnedEntryMap_entry transient = {0};
        OwnedEntryMap_entry moved = {0};
        cmeta_range range;
        cmeta_range_cursor cursor = {0};
        cmeta_collector collector;

        owned_entry_fail_copy_at = 0u;
        owned_entry_copy_calls = 0u;
        check_equal(OwnedEntryMap_init(&source, 1u), TURBO_STL_OK);
        check_equal(OwnedEntryMap_put(&source, input.key, input.value),
                    TURBO_STL_OK);
        check_true(cmeta_container_range_view(&source,
                                              CMETA_CONTAINER_VIEW_DEFAULT,
                                              &range));
        check_equal(cmeta_type_require_traits(
                        range.element_type,
                        CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                            CMETA_TRAIT_DESTROY | CMETA_TRAIT_EQUAL |
                            CMETA_TRAIT_HASH), CMETA_OK);
        {
            cmeta_gen_status status = cmeta_range_next(&range, &cursor,
                                                       &borrowed);
            check_true(status == CMETA_GEN_VALUE ||
                       status == CMETA_GEN_VALUE_AND_DONE);
        }
        check_true(range.element_type->traits->copy_construct(&transient,
                                                               &borrowed));
        range.element_type->traits->move_construct(&moved, &transient);
        check_null(transient.key.value);
        check_null(transient.value.value);

        collector = OwnedEntryMap_collector(&output, 1u);
        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, range.element_type,
                                           &moved), CMETA_OK);
        check_equal(cmeta_collector_finish(&collector), CMETA_OK);
        check_equal(*OwnedEntryMap_get_const(&output, moved.key)->value, 10);

        range.element_type->traits->destroy(&transient);
        range.element_type->traits->destroy(&moved);
        OwnedEntryMap_destroy(&output);
        OwnedEntryMap_destroy(&source);
        owned_entry_destroy(&input.value);
        owned_entry_destroy(&input.key);
        check_equal(owned_entry_live, (size_t)0u);
        check_true(owned_entry_move_calls >= (size_t)2u);
    }

    it("rolls back the key when value copy construction fails") {
        OwnedEntryTree_entry source = {owned_entry_make(2),
                                       owned_entry_make(20)};
        OwnedEntryTree_entry destination = {0};
        size_t live_before = owned_entry_live;

        owned_entry_copy_calls = 0u;
        owned_entry_fail_copy_at = 2u;
        check_false(OwnedEntryTree_entry_cmeta_type.traits->copy_construct(
            &destination, &source));
        check_equal(owned_entry_live, live_before);
        owned_entry_fail_copy_at = 0u;
        owned_entry_destroy(&source.value);
        owned_entry_destroy(&source.key);
        check_equal(owned_entry_live, (size_t)0u);
    }

    it("rejects an equivalent entry descriptor without semantic traits") {
        OwnedEntryMap output = {0};
        cmeta_type_desc missing = OwnedEntryMap_entry_cmeta_type;
        cmeta_collector collector = OwnedEntryMap_collector(&output, 1u);

        missing.traits = NULL;
        collector.input_type = &missing;
        check_equal(cmeta_collector_begin(&collector), CMETA_TRAIT_MISSING);
        check_equal(memcmp(&output, &(OwnedEntryMap){0}, sizeof(output)), 0);
    }
}
