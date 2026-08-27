#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct managed_stream_value {
    int *resource;
} managed_stream_value;

#define CMETA_USER_TYPE_LIST \
    , (O, managed_stream_value, cmeta_type_managed_stream_value, \
       CMETA_T_OBJECT, cmeta_traits_managed_stream_value)
#define CMETA_CALLABLE_TYPE_LIST CMETA_BUILTIN_TYPE_LIST

#include <turbostl/stream.h>
#include "tinytest.h"

static size_t managed_stream_copies;
static size_t managed_stream_moves;
static size_t managed_stream_destroys;

static managed_stream_value managed_stream_make(int value) {
    managed_stream_value result = {0};
    result.resource = (int *)malloc(sizeof(*result.resource));
    if (result.resource) *result.resource = value;
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
    free(value->resource);
    value->resource = NULL;
}

const cmeta_type_traits cmeta_traits_managed_stream_value = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
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

spec("TurboSTL managed CFlow Stream") {
    it("collects independently owned managed values") {
        managed_stream_value values[] = {
            managed_stream_make(4), managed_stream_make(12)
        };
        ManagedStreamList input = {0};
        ManagedStreamList output = {0};
        turbostl_stream_t pipeline = {0};
        turbostl_collect_result result;
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
        turbostl_stream_destroy(&pipeline);
        ManagedStreamList_destroy(&input);
        managed_stream_destroy(&values[0]);
        managed_stream_destroy(&values[1]);
        check_equal(managed_stream_copies, (size_t)6u);
        check_equal(managed_stream_moves, (size_t)2u);
        check_equal(managed_stream_destroys, (size_t)10u);
    }

    it("destroys skipped values and retains taken values independently") {
        managed_stream_value values[] = {
            managed_stream_make(4),
            managed_stream_make(12),
            managed_stream_make(20)
        };
        ManagedStreamList input = {0};
        ManagedStreamList output = {0};
        turbostl_stream_t pipeline = {0};
        turbostl_collect_result result;
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
        turbostl_stream_destroy(&pipeline);
        ManagedStreamList_destroy(&input);
        managed_stream_destroy(&values[0]);
        managed_stream_destroy(&values[1]);
        managed_stream_destroy(&values[2]);
        check_equal(managed_stream_copies, (size_t)8u);
        check_equal(managed_stream_moves, (size_t)1u);
        check_equal(managed_stream_destroys, (size_t)12u);
    }
}
