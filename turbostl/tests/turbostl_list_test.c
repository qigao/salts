#include <turbostl/typed.h>
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

typedef struct list_owned_value {
    int *value;
} list_owned_value;

static size_t list_owned_live;
static size_t list_copy_count;
static size_t list_fail_copy_at;

static bool list_owned_copy(void *destination_, const void *source_) {
    list_owned_value *destination = (list_owned_value *)destination_;
    const list_owned_value *source = (const list_owned_value *)source_;
    if (list_fail_copy_at != 0u && list_copy_count + 1u == list_fail_copy_at)
        return false;
    destination->value = (int *)malloc(sizeof(*destination->value));
    if (destination->value == NULL) return false;
    *destination->value = *source->value;
    ++list_copy_count;
    ++list_owned_live;
    return true;
}

static void list_owned_move(void *destination_, void *source_) {
    list_owned_value *destination = (list_owned_value *)destination_;
    list_owned_value *source = (list_owned_value *)source_;
    destination->value = source->value;
    source->value = NULL;
}

static void list_owned_destroy(void *value_) {
    list_owned_value *value = (list_owned_value *)value_;
    if (value != NULL && value->value != NULL) {
        free(value->value);
        value->value = NULL;
        --list_owned_live;
    }
}

static const cmeta_type_traits list_owned_traits = {
    CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    NULL, NULL, NULL, list_owned_copy, list_owned_move, list_owned_destroy
};

static const cmeta_type_desc list_owned_type = {
    "list_owned", sizeof(list_owned_value), _Alignof(list_owned_value),
    CMETA_T_OBJECT, NULL, &list_owned_traits
};

static list_owned_value list_owned_make(int value) {
    list_owned_value result;
    result.value = (int *)malloc(sizeof(*result.value));
    if (result.value != NULL) {
        *result.value = value;
        ++list_owned_live;
    }
    return result;
}

spec("Independent linked List") {
    it("keeps existing nodes stable across middle insertion") {
        list_t list = {0};
        list_iter_t first;
        list_iter_t second;
        list_iter_t inserted;
        int one = 1;
        int two = 2;
        int middle = 7;

        check_equal(list_init_bytes(&list, sizeof(int), 64u, 3u),
                    STL_OK);
        check_equal(list_push_back(&list, &one, &first), STL_OK);
        check_equal(list_push_back(&list, &two, &second), STL_OK);
        check_equal(list_insert_after(&list, first, &middle, &inserted),
                    STL_OK);
        check_equal(*(const int *)list_iter_value_const(second), 2);
        check_equal(*(const int *)list_iter_value_const(inserted), 7);
        check_equal(*(const int *)list_iter_value_const(first), 1);
        check_equal((uintptr_t)list_front(&list) % 64u, (uintptr_t)0u);
        list_raw_destroy_storage(&list);
    }

    it("supports bidirectional iteration exact erase and hard limits") {
        list_t list = {0};
        list_t other = {0};
        list_iter_t first;
        list_iter_t second;
        list_iter_t iterator;
        uint64_t generation;
        int values[] = {1, 2, 3};
        int out = -1;

        check_equal(list_init_bytes(&list, sizeof(int),
                                          _Alignof(int), 3u), STL_OK);
        check_equal(list_init_bytes(&other, sizeof(int),
                                          _Alignof(int), 1u), STL_OK);
        check_equal(list_push_back(&list, &values[0], &first),
                    STL_OK);
        check_equal(list_push_back(&list, &values[2], &second),
                    STL_OK);
        check_equal(list_insert_before(&list, second, &values[1],
                                             &iterator), STL_OK);
        iterator = list_begin(&list);
        check_equal(*(const int *)list_iter_value_const(iterator), 1);
        check_equal(list_iter_next(&iterator), STL_OK);
        check_equal(*(const int *)list_iter_value_const(iterator), 2);
        check_equal(list_iter_next(&iterator), STL_OK);
        check_equal(*(const int *)list_iter_value_const(iterator), 3);
        check_equal(list_iter_prev(&iterator), STL_OK);
        check_equal(*(const int *)list_iter_value_const(iterator), 2);

        generation = list_generation(&list);
        check_equal(list_push_back(&list, &values[0], NULL),
                    STL_CAPACITY_EXCEEDED);
        check_equal(list_generation(&list), generation);
        check_equal(list_erase(&other, first, &out),
                    STL_INVALID_ARGUMENT);
        check_equal(list_generation(&list), generation);
        check_equal(list_erase(&list, iterator, &out), STL_OK);
        check_equal(out, 2);
        check_equal(list_size(&list), (size_t)2u);
        list_clear(&list);
        check_true(list_empty(&list));
        check_equal(list_push_front(&list, &values[0], NULL),
                    STL_OK);
        list_raw_destroy_storage(&other);
        generation = list_generation(&list);
        list_raw_destroy_storage(&list);
        check_equal(list_generation(&list), generation + 1u);
    }

    it("rolls back owning copy and from failures and balances pool reuse") {
        list_t list = {0};
        list_t before;
        list_owned_value values[2];
        uint64_t generation;

        list_owned_live = 0u;
        list_copy_count = 0u;
        list_fail_copy_at = 0u;
        values[0] = list_owned_make(10);
        values[1] = list_owned_make(20);
        check_equal(list_raw_init(&list, &list_owned_type, 2u),
                    STL_OK);
        generation = list_generation(&list);
        list_fail_copy_at = list_copy_count + 1u;
        check_equal(list_push_back(&list, &values[0], NULL),
                    STL_OUT_OF_MEMORY);
        check_equal(list_size(&list), (size_t)0u);
        check_equal(list_generation(&list), generation);
        list_fail_copy_at = 0u;
        check_equal(list_push_back(&list, &values[0], NULL), STL_OK);
        list_clear(&list);
        check_equal(list_push_front(&list, &values[1], NULL), STL_OK);
        list_raw_destroy_storage(&list);

        before = list;
        list_fail_copy_at = list_copy_count + 2u;
        check_equal(list_raw_from_array(&list, values, 2u,
                                          &list_owned_type, 2u),
                    STL_OUT_OF_MEMORY);
        check_equal(memcmp(&list, &before, sizeof(list)), 0);
        list_fail_copy_at = 0u;
        list_owned_destroy(&values[1]);
        list_owned_destroy(&values[0]);
        check_equal(list_owned_live, (size_t)0u);
    }

    it("walks a stable link cursor in linear order") {
        List(int, list);
        cmeta_range range;
        cmeta_range_cursor cursor = {0};
        cmeta_range_cursor stale_cursor = {0};
        int out = -1;
        int stale_out = -7;
        int value;

        check_equal(list_init(&list, 65u), STL_OK);
        for (value = 0; value < 64; ++value)
            check_equal(list_push_back(&list, &value, NULL), STL_OK);
        check_true(cmeta_container_range_view(&list,
                                              CMETA_CONTAINER_VIEW_DEFAULT,
                                              &range));
        for (value = 0; value < 64; ++value) {
            cmeta_gen_status status = cmeta_range_next(&range, &cursor, &out);
            check_true(status == CMETA_GEN_VALUE ||
                       status == CMETA_GEN_VALUE_AND_DONE);
            check_equal(out, value);
        }
        check_equal(cmeta_range_next(&range, &cursor, &out), CMETA_GEN_DONE);

        check_true(cmeta_container_range_view(&list,
                                              CMETA_CONTAINER_VIEW_DEFAULT,
                                              &range));
        check_equal(cmeta_range_next(&range, &stale_cursor, &stale_out),
                    CMETA_GEN_VALUE);
        check_equal(stale_out, 0);
        check_equal(list_pop_front(&list, NULL), STL_OK);
        stale_out = -7;
        {
            cmeta_range_cursor before_cursor = stale_cursor;
            check_equal(cmeta_range_next(&range, &stale_cursor, &stale_out),
                        CMETA_GEN_MUTATED);
            check_equal(memcmp(&stale_cursor, &before_cursor,
                               sizeof(stale_cursor)), 0);
            check_equal(stale_out, -7);
        }
        list_destroy(&list);
    }
}
