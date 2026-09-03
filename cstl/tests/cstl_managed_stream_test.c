#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct managed_stream_value {
    int *resource;
} managed_stream_value;

#define CMETA_USER_TYPE_LIST \
    , (O, managed_stream_value, cmeta_type_managed_stream_value, \
       CMETA_T_OBJECT, cmeta_traits_managed_stream_value)
#define CMETA_CALLABLE_TYPE_LIST CMETA_BUILTIN_TYPE_LIST

#include <cstl/stream.h>
#include "tinytest.h"

static size_t managed_stream_copies;
static size_t managed_stream_moves;
static size_t managed_stream_destroys;
static size_t managed_stream_live_resources;

static managed_stream_value managed_stream_make(int value) {
    managed_stream_value result = {0};
    result.resource = (int *)malloc(sizeof(*result.resource));
    if (result.resource) {
        *result.resource = value;
        ++managed_stream_live_resources;
    }
    return result;
}

static bool managed_stream_copy(void *destination_, const void *source_) {
    managed_stream_value *destination = (managed_stream_value *)destination_;
    const managed_stream_value *source =
        (const managed_stream_value *)source_;
    ++managed_stream_copies;
    destination->resource = NULL;
    if (!source->resource) return true;
    destination->resource = (int *)malloc(sizeof(*destination->resource));
    if (!destination->resource) return false;
    *destination->resource = *source->resource;
    ++managed_stream_live_resources;
    return true;
}

static void managed_stream_move(void *destination_, void *source_) {
    managed_stream_value *destination = (managed_stream_value *)destination_;
    managed_stream_value *source = (managed_stream_value *)source_;
    ++managed_stream_moves;
    destination->resource = source->resource;
    source->resource = NULL;
}

static void managed_stream_destroy(void *value_) {
    managed_stream_value *value = (managed_stream_value *)value_;
    ++managed_stream_destroys;
    if (value->resource) --managed_stream_live_resources;
    free(value->resource);
    value->resource = NULL;
}

static bool managed_stream_equal(const void *left_, const void *right_) {
    const managed_stream_value *left =
        (const managed_stream_value *)left_;
    const managed_stream_value *right =
        (const managed_stream_value *)right_;
    if (!left->resource || !right->resource)
        return left->resource == right->resource;
    return *left->resource == *right->resource;
}

static uint64_t managed_stream_hash(const void *value_) {
    const managed_stream_value *value =
        (const managed_stream_value *)value_;
    return value->resource
        ? (uint64_t)(uint32_t)*value->resource * UINT64_C(11400714819323198485)
        : UINT64_C(0);
}

static int managed_stream_compare(const void *left_, const void *right_) {
    const managed_stream_value *left =
        (const managed_stream_value *)left_;
    const managed_stream_value *right =
        (const managed_stream_value *)right_;
    const int left_value = left->resource ? *left->resource : 0;
    const int right_value = right->resource ? *right->resource : 0;
    return (left_value > right_value) - (left_value < right_value);
}

const cmeta_type_traits cmeta_traits_managed_stream_value = {
    .flags = CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH | CMETA_TRAIT_COMPARE |
             CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .equal = managed_stream_equal,
    .hash = managed_stream_hash,
    .compare = managed_stream_compare,
    .copy_construct = managed_stream_copy,
    .move_construct = managed_stream_move,
    .destroy = managed_stream_destroy
};

const cmeta_type_desc cmeta_type_managed_stream_value = {
    .name = "managed_stream_value",
    .size = sizeof(managed_stream_value),
    .align = _Alignof(managed_stream_value),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &cmeta_traits_managed_stream_value,
    .identity = NULL
};

const cmeta_type_desc cmeta_type_managed_stream_value_ptr = {
    .name = "managed_stream_value *",
    .size = sizeof(managed_stream_value *),
    .align = _Alignof(managed_stream_value *),
    .kind = CMETA_T_POINTER,
    .pointee = &cmeta_type_managed_stream_value,
    .traits = NULL,
    .identity = NULL
};

typed(List, ManagedStreamList, managed_stream_value);

spec("CSTL managed CFlow Stream") {
    it("counts managed values without retaining terminal copies") {
        managed_stream_value values[] = {
            managed_stream_make(4), managed_stream_make(12)
        };
        ManagedStreamList input = {0};
        cstl_stream_t pipeline = {0};
        const char *error = NULL;
        size_t count = 99u;

        managed_stream_copies = 0u;
        managed_stream_moves = 0u;
        managed_stream_destroys = 0u;
        check_equal(managed_stream_live_resources, (size_t)2u);
        check_equal(ManagedStreamList_init(&input, 2u), STL_OK);
        check_equal(ManagedStreamList_push_back(&input, values[0]), STL_OK);
        check_equal(ManagedStreamList_push_back(&input, values[1]), STL_OK);
        check_not_null(stream(&input, &pipeline));

        check_true(cstl_stream_count(&pipeline, &count, &error));
        check_equal(count, (size_t)2u);
        check_null(error);

        cstl_stream_destroy(&pipeline);
        ManagedStreamList_destroy(&input);
        managed_stream_destroy(&values[0]);
        managed_stream_destroy(&values[1]);
        check_equal(managed_stream_copies, (size_t)4u);
        check_equal(managed_stream_moves, (size_t)0u);
        check_equal(managed_stream_destroys, (size_t)6u);
        check_equal(managed_stream_live_resources, (size_t)0u);
    }

    it("sorts managed values with independent balanced ownership") {
        managed_stream_value values[] = {
            managed_stream_make(12),
            managed_stream_make(4),
            managed_stream_make(8)
        };
        ManagedStreamList input = {0};
        ManagedStreamList output = {0};
        cstl_stream_t pipeline = {0};
        cstl_collect_result result;
        managed_stream_value first = {0};
        managed_stream_value second = {0};
        managed_stream_value third = {0};

        managed_stream_copies = 0u;
        managed_stream_moves = 0u;
        managed_stream_destroys = 0u;
        check_equal(managed_stream_live_resources, (size_t)3u);
        check_equal(ManagedStreamList_init(&input, 3u), STL_OK);
        check_equal(ManagedStreamList_push_back(&input, values[0]), STL_OK);
        check_equal(ManagedStreamList_push_back(&input, values[1]), STL_OK);
        check_equal(ManagedStreamList_push_back(&input, values[2]), STL_OK);
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.sorted(&pipeline, 3u));

        result = to_list_typed(
            &pipeline, ManagedStreamList, &output, 3u);
        check_true(result.ok);
        check_equal(result.count, (size_t)3u);
        check_equal(ManagedStreamList_pop_front(&output, &first), STL_OK);
        check_equal(ManagedStreamList_pop_front(&output, &second), STL_OK);
        check_equal(ManagedStreamList_pop_front(&output, &third), STL_OK);
        check_equal(*first.resource, 4);
        check_equal(*second.resource, 8);
        check_equal(*third.resource, 12);

        managed_stream_destroy(&first);
        managed_stream_destroy(&second);
        managed_stream_destroy(&third);
        ManagedStreamList_destroy(&output);
        cstl_stream_destroy(&pipeline);
        ManagedStreamList_destroy(&input);
        managed_stream_destroy(&values[0]);
        managed_stream_destroy(&values[1]);
        managed_stream_destroy(&values[2]);
        check_equal(managed_stream_live_resources, (size_t)0u);
    }

    it("keeps distinct managed values independently owned and balanced") {
        managed_stream_value values[] = {
            managed_stream_make(4),
            managed_stream_make(12),
            managed_stream_make(4)
        };
        ManagedStreamList input = {0};
        ManagedStreamList output = {0};
        cstl_stream_t pipeline = {0};
        cstl_collect_result result;
        managed_stream_value first = {0};
        managed_stream_value second = {0};

        managed_stream_copies = 0u;
        managed_stream_moves = 0u;
        managed_stream_destroys = 0u;
        check_equal(managed_stream_live_resources, (size_t)3u);
        check_equal(ManagedStreamList_init(&input, 3u), STL_OK);
        check_equal(ManagedStreamList_push_back(&input, values[0]), STL_OK);
        check_equal(ManagedStreamList_push_back(&input, values[1]), STL_OK);
        check_equal(ManagedStreamList_push_back(&input, values[2]), STL_OK);
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.distinct(&pipeline, 2u));

        result = to_list_typed(
            &pipeline, ManagedStreamList, &output, 2u);
        check_true(result.ok);
        check_equal(result.count, (size_t)2u);
        check_equal(ManagedStreamList_pop_front(&output, &first), STL_OK);
        check_equal(ManagedStreamList_pop_front(&output, &second), STL_OK);
        check_equal(*first.resource, 4);
        check_equal(*second.resource, 12);

        managed_stream_destroy(&first);
        managed_stream_destroy(&second);
        ManagedStreamList_destroy(&output);
        cstl_stream_destroy(&pipeline);
        ManagedStreamList_destroy(&input);
        managed_stream_destroy(&values[0]);
        managed_stream_destroy(&values[1]);
        managed_stream_destroy(&values[2]);
        check_equal(managed_stream_live_resources, (size_t)0u);
    }

    it("collects independently owned managed values") {
        managed_stream_value values[] = {
            managed_stream_make(4), managed_stream_make(12)
        };
        ManagedStreamList input = {0};
        ManagedStreamList output = {0};
        cstl_stream_t pipeline = {0};
        cstl_collect_result result;
        managed_stream_value first = {0};
        managed_stream_value second = {0};

        managed_stream_copies = 0u;
        managed_stream_moves = 0u;
        managed_stream_destroys = 0u;
        check_equal(ManagedStreamList_init(&input, 2u), STL_OK);
        check_equal(ManagedStreamList_push_back(&input, values[0]), STL_OK);
        check_equal(ManagedStreamList_push_back(&input, values[1]), STL_OK);
        check_not_null(stream(&input, &pipeline));

        result = to_list_typed(
            &pipeline, ManagedStreamList, &output, 2u);
        check_true(result.ok);
        check_equal(result.status, CMETA_OK);
        check_equal(result.count, (size_t)2u);
        check_equal(ManagedStreamList_pop_front(&output, &first), STL_OK);
        check_equal(ManagedStreamList_pop_front(&output, &second), STL_OK);
        check_equal(*first.resource, 4);
        check_equal(*second.resource, 12);

        managed_stream_destroy(&first);
        managed_stream_destroy(&second);
        ManagedStreamList_destroy(&output);
        cstl_stream_destroy(&pipeline);
        ManagedStreamList_destroy(&input);
        managed_stream_destroy(&values[0]);
        managed_stream_destroy(&values[1]);
        check_equal(managed_stream_copies, (size_t)6u);
        check_equal(managed_stream_moves, (size_t)2u);
        check_equal(managed_stream_destroys, (size_t)10u);
    }

    it("destroys skipped values and keeps the taken value independently owned") {
        managed_stream_value values[] = {
            managed_stream_make(4),
            managed_stream_make(12),
            managed_stream_make(20)
        };
        ManagedStreamList input = {0};
        ManagedStreamList output = {0};
        cstl_stream_t pipeline = {0};
        cstl_collect_result result;
        managed_stream_value selected = {0};

        managed_stream_copies = 0u;
        managed_stream_moves = 0u;
        managed_stream_destroys = 0u;
        check_equal(ManagedStreamList_init(&input, 3u), STL_OK);
        check_equal(ManagedStreamList_push_back(&input, values[0]), STL_OK);
        check_equal(ManagedStreamList_push_back(&input, values[1]), STL_OK);
        check_equal(ManagedStreamList_push_back(&input, values[2]), STL_OK);
        check_not_null(stream(&input, &pipeline));
        check_not_null(pipeline.skip(&pipeline, 1u)->take(&pipeline, 1u));

        result = to_list_typed(
            &pipeline, ManagedStreamList, &output, 1u);
        check_true(result.ok);
        check_equal(result.status, CMETA_OK);
        check_equal(result.count, (size_t)1u);
        check_equal(ManagedStreamList_pop_front(&output, &selected), STL_OK);
        check_equal(*selected.resource, 12);

        managed_stream_destroy(&selected);
        ManagedStreamList_destroy(&output);
        cstl_stream_destroy(&pipeline);
        ManagedStreamList_destroy(&input);
        managed_stream_destroy(&values[0]);
        managed_stream_destroy(&values[1]);
        managed_stream_destroy(&values[2]);
        check_equal(managed_stream_copies, (size_t)8u);
        check_equal(managed_stream_moves, (size_t)1u);
        check_equal(managed_stream_destroys, (size_t)12u);
    }

    it("retains and destroys a managed find_first result exactly once") {
        managed_stream_value values[] = {
            managed_stream_make(7), managed_stream_make(13)
        };
        ManagedStreamList input = {0};
        cstl_stream_t pipeline = {0};
        cstl_find_result found = {0};
        const managed_stream_value *selected;
        const char *error = NULL;

        managed_stream_copies = 0u;
        managed_stream_moves = 0u;
        managed_stream_destroys = 0u;
        check_equal(ManagedStreamList_init(&input, 2u), STL_OK);
        check_equal(ManagedStreamList_push_back(&input, values[0]), STL_OK);
        check_equal(ManagedStreamList_push_back(&input, values[1]), STL_OK);
        check_not_null(stream(&input, &pipeline));

        check_true(cstl_stream_find_first(
            &pipeline, &found, &error));
        check_null(error);
        check_true(cstl_find_result_has_value(&found));
        check_true(cmeta_type_equal(
            cstl_find_result_type(&found),
            &cmeta_type_managed_stream_value));
        selected = (const managed_stream_value *)
            cstl_find_result_value(&found);
        check_not_null(selected);
        check_not_null(selected->resource);
        check_equal(*selected->resource, 7);
        check_true(selected->resource !=
                   ManagedStreamList_front_const(&input)->resource);

        cstl_find_result_destroy(&found);
        cstl_stream_destroy(&pipeline);
        ManagedStreamList_destroy(&input);
        managed_stream_destroy(&values[0]);
        managed_stream_destroy(&values[1]);
        check_equal(managed_stream_copies, (size_t)4u);
        check_equal(managed_stream_moves, (size_t)0u);
        check_equal(managed_stream_destroys, (size_t)6u);
    }
}
