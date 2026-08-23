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

#include "legacy_instance_typed.h"
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
    .name = "owned_entry_value",
    .size = sizeof(owned_entry_value),
    .align = _Alignof(owned_entry_value),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &cmeta_traits_owned_entry_value,
    .identity = NULL
};

const cmeta_type_desc cmeta_type_owned_entry_value_ptr = {
    .name = "owned_entry_value *",
    .size = sizeof(owned_entry_value *),
    .align = _Alignof(owned_entry_value *),
    .kind = CMETA_T_POINTER,
    .pointee = &cmeta_type_owned_entry_value,
    .traits = NULL,
    .identity = NULL
};

spec("TurboSTL composed entry descriptors") {
    it("copies a borrowed Range entry into an owned collector transient") {
        HashMap(owned_entry_value, owned_entry_value, source);
        HashMap(owned_entry_value, owned_entry_value, output);
        owned_entry_value input_key = owned_entry_make(1);
        owned_entry_value input_value = owned_entry_make(10);
        cmeta_entry borrowed = {0};
        cmeta_entry transient = {0};
        cmeta_entry moved = {0};
        cmeta_range range;
        cmeta_range_cursor cursor = {0};
        cmeta_collector collector;
        const cmeta_container_desc *output_desc;
        const owned_entry_value *stored;
        size_t moves_before;

        owned_entry_fail_copy_at = 0u;
        owned_entry_copy_calls = 0u;
        owned_entry_move_calls = 0u;
        check_equal(hash_map_init(&source, 1u), STL_OK);
        check_equal(hash_map_put(&source, &input_key, &input_value), STL_OK);
        check_true(cmeta_container_range_view(&source,
                                              CMETA_CONTAINER_VIEW_DEFAULT,
                                              &range));
        check_true(cmeta_type_equal(range.element_type,
                                    &cmeta_type_hash_entry));
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
        check_not_null(transient.key_storage);
        check_not_null(transient.value_storage);
        moves_before = owned_entry_move_calls;
        range.element_type->traits->move_construct(&moved, &transient);
        check_equal(owned_entry_move_calls, moves_before);
        check_null(transient.key);
        check_null(transient.value);
        check_null(transient.key_storage);
        check_null(transient.value_storage);

        output_desc = cmeta_container_descriptor(&output);
        check_not_null(output_desc);
        check_not_null(output_desc->collector);
        collector = output_desc->collector(&output, 1u);
        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, range.element_type,
                                           &moved), CMETA_OK);
        check_equal(cmeta_collector_finish(&collector), CMETA_OK);
        stored = (const owned_entry_value *)hash_map_get_const(&output,
                                                               moved.key);
        check_true(stored != NULL && stored->value != NULL);
        check_equal(*stored->value, 10);

        range.element_type->traits->destroy(&transient);
        range.element_type->traits->destroy(&moved);
        hash_map_destroy(&output);
        hash_map_destroy(&source);
        owned_entry_destroy(&input_value);
        owned_entry_destroy(&input_key);
        check_equal(owned_entry_live, (size_t)0u);
    }

    it("rolls back the key when value copy construction fails") {
        owned_entry_value key = owned_entry_make(2);
        owned_entry_value value = owned_entry_make(20);
        cmeta_entry source = {
            .key_type = &cmeta_type_owned_entry_value,
            .value_type = &cmeta_type_owned_entry_value,
            .key = &key,
            .value = &value,
            .key_storage = NULL,
            .value_storage = NULL
        };
        cmeta_entry destination = {0};
        size_t live_before = owned_entry_live;

        owned_entry_copy_calls = 0u;
        owned_entry_fail_copy_at = 2u;
        check_false(cmeta_type_ordered_entry.traits->copy_construct(
            &destination, &source));
        check_equal(owned_entry_live, live_before);
        check_null(destination.key);
        check_null(destination.value);
        check_null(destination.key_storage);
        check_null(destination.value_storage);
        owned_entry_fail_copy_at = 0u;
        owned_entry_destroy(&value);
        owned_entry_destroy(&key);
        check_equal(owned_entry_live, (size_t)0u);
    }

    it("rejects an equivalent entry descriptor without semantic traits") {
        Map(owned_entry_value, owned_entry_value, output);
        const cmeta_container_desc *desc = cmeta_container_descriptor(&output);
        cmeta_type_desc missing = cmeta_type_ordered_entry;
        cmeta_collector collector;

        check_not_null(desc);
        check_not_null(desc->collector);
        missing.traits = NULL;
        collector = desc->collector(&output, 1u);
        collector.input_type = &missing;
        check_equal(cmeta_collector_begin(&collector), CMETA_TRAIT_MISSING);
        check_true(cmeta_container_descriptor(&output) == desc);
        check_equal(map_init(&output, 1u), STL_OK);
        check_true(map_empty(&output));
        map_destroy(&output);
    }
}
