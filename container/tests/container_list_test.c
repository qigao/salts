#include <turbo/container/typed.h>
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

typed(List, RangeIntList, int);

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
    it("keeps bidirectional order and indexes from either end") {
        turbo_list_t list = {0};
        int values[] = {1, 2, 3, 4};
        int out = -1;

        check_equal(turbo_list_init_bytes(&list, sizeof(int), 64u, 4u),
                    CONTAINER_OK);
        check_equal(turbo_list_reserve(&list, 4u), CONTAINER_OK);
        check_equal(turbo_list_capacity(&list), (size_t)4u);
        check_equal(turbo_list_push_back(&list, &values[1]), CONTAINER_OK);
        check_equal(turbo_list_push_front(&list, &values[0]), CONTAINER_OK);
        check_equal(turbo_list_push_back(&list, &values[2]), CONTAINER_OK);
        check_equal(turbo_list_push_back(&list, &values[3]), CONTAINER_OK);
        check_true(list.head != NULL && list.tail != NULL);
        check_equal((uintptr_t)turbo_list_front(&list) % 64u, (uintptr_t)0u);
        {
            uint64_t generation = turbo_list_generation(&list);
            check_equal(turbo_list_push_back(&list, &values[0]),
                        CONTAINER_CAPACITY_EXCEEDED);
            check_equal(turbo_list_generation(&list), generation);
        }
        check_equal(*(const int *)turbo_list_front_const(&list), 1);
        check_equal(*(const int *)turbo_list_back_const(&list), 4);
        check_equal(*(const int *)turbo_list_at_const(&list, 1u), 2);
        check_equal(*(const int *)turbo_list_at_const(&list, 3u), 4);
        check_equal(turbo_list_pop_front(&list, &out), CONTAINER_OK);
        check_equal(out, 1);
        check_equal(turbo_list_pop_back(&list, &out), CONTAINER_OK);
        check_equal(out, 4);
        check_equal(turbo_list_capacity(&list), (size_t)4u);
        turbo_list_destroy(&list);
    }

    it("reserves transactionally retains capacity on clear and enforces limit") {
        turbo_list_t list = {0};
        uint64_t generation;
        int value = 7;

        check_equal(turbo_list_init_bytes(&list, sizeof(value),
                                          _Alignof(int), 3u), CONTAINER_OK);
        {
            int unchanged = 91;
            check_equal(turbo_list_pop_front(&list, &unchanged),
                        CONTAINER_EMPTY);
            check_equal(unchanged, 91);
        }
        generation = turbo_list_generation(&list);
        check_equal(turbo_list_reserve(&list, 3u), CONTAINER_OK);
        check_equal(turbo_list_generation(&list), generation);
        check_equal(turbo_list_reserve(&list, 4u),
                    CONTAINER_CAPACITY_EXCEEDED);
        check_equal(turbo_list_capacity(&list), (size_t)3u);
        check_equal(turbo_list_generation(&list), generation);
        check_equal(turbo_list_push_back(&list, &value), CONTAINER_OK);
        turbo_list_clear(&list);
        check_equal(turbo_list_capacity(&list), (size_t)3u);
        check_true(turbo_list_empty(&list));
        check_equal(turbo_list_push_front(&list, &value), CONTAINER_OK);
        generation = turbo_list_generation(&list);
        turbo_list_destroy(&list);
        check_equal(turbo_list_generation(&list), generation + 1u);
        turbo_list_destroy(&list);
        check_equal(turbo_list_generation(&list), generation + 1u);
        check_equal(turbo_list_init_bytes(&list, sizeof(value),
                                          _Alignof(int), 1u), CONTAINER_OK);
        check_equal(turbo_list_generation(&list), generation + 2u);
        turbo_list_destroy(&list);
    }

    it("rolls back owning copy and from failures and balances pool reuse") {
        turbo_list_t list = {0};
        turbo_list_t before;
        list_owned_value values[2];
        uint64_t generation;

        list_owned_live = 0u;
        list_copy_count = 0u;
        list_fail_copy_at = 0u;
        values[0] = list_owned_make(10);
        values[1] = list_owned_make(20);
        check_equal(turbo_list_init(&list, &list_owned_type, 2u),
                    CONTAINER_OK);
        check_equal(turbo_list_reserve(&list, 2u), CONTAINER_OK);
        generation = turbo_list_generation(&list);
        list_fail_copy_at = list_copy_count + 1u;
        check_equal(turbo_list_push_back(&list, &values[0]),
                    CONTAINER_OUT_OF_MEMORY);
        check_equal(turbo_list_size(&list), (size_t)0u);
        check_equal(turbo_list_capacity(&list), (size_t)2u);
        check_equal(turbo_list_generation(&list), generation);
        list_fail_copy_at = 0u;
        check_equal(turbo_list_push_back(&list, &values[0]), CONTAINER_OK);
        turbo_list_clear(&list);
        check_equal(turbo_list_push_front(&list, &values[1]), CONTAINER_OK);
        turbo_list_destroy(&list);

        before = list;
        list_fail_copy_at = list_copy_count + 2u;
        check_equal(turbo_list_from_array(&list, values, 2u,
                                          &list_owned_type, 2u),
                    CONTAINER_OUT_OF_MEMORY);
        check_equal(memcmp(&list, &before, sizeof(list)), 0);
        list_fail_copy_at = 0u;
        list_owned_destroy(&values[1]);
        list_owned_destroy(&values[0]);
        check_equal(list_owned_live, (size_t)0u);
    }

    it("walks a stable link cursor in linear order") {
        RangeIntList list = {0};
        cmeta_range range;
        cmeta_range_cursor cursor = 0u;
        cmeta_range_cursor stale_cursor = 0u;
        int out = -1;
        int stale_out = -7;
        int value;

        check_equal(RangeIntList_init(&list, 65u), CONTAINER_OK);
        for (value = 0; value < 64; ++value)
            check_equal(RangeIntList_push_back(&list, value), CONTAINER_OK);
        range = RangeIntList_range(&list);
        for (value = 0; value < 64; ++value) {
            cmeta_gen_status status = cmeta_range_next(&range, &cursor, &out);
            check_true(status == CMETA_GEN_VALUE ||
                       status == CMETA_GEN_VALUE_AND_DONE);
            check_equal(out, value);
        }
        check_equal(cmeta_range_next(&range, &cursor, &out), CMETA_GEN_DONE);

        range = RangeIntList_range(&list);
        {
            uint64_t generation = turbo_list_generation(&list.raw);
            check_equal(turbo_list_reserve(&list.raw, 65u), CONTAINER_OK);
            check_equal(turbo_list_generation(&list.raw), generation);
        }
        check_equal(cmeta_range_next(&range, &stale_cursor, &stale_out),
                    CMETA_GEN_VALUE);
        check_equal(stale_out, 0);
        check_equal(RangeIntList_pop_front(&list, NULL), CONTAINER_OK);
        stale_out = -7;
        {
            cmeta_range_cursor before_cursor = stale_cursor;
            check_equal(cmeta_range_next(&range, &stale_cursor, &stale_out),
                        CMETA_GEN_MUTATED);
            check_equal(stale_cursor, before_cursor);
            check_equal(stale_out, -7);
        }
        RangeIntList_destroy(&list);
    }
}
