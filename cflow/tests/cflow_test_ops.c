#include "cflow_test_ops.h"

#include <stdlib.h>

static bool cflow_test_owned_value_copy(void *destination_,
                                        const void *source_) {
    cflow_test_owned_value *destination =
        (cflow_test_owned_value *)destination_;
    const cflow_test_owned_value *source =
        (const cflow_test_owned_value *)source_;

    destination->resource = NULL;
    if (source->resource == NULL)
        return true;
    destination->resource = (int *)malloc(sizeof(*destination->resource));
    if (destination->resource == NULL)
        return false;
    *destination->resource = *source->resource;
    return true;
}

static void cflow_test_owned_value_move(void *destination_, void *source_) {
    cflow_test_owned_value *destination =
        (cflow_test_owned_value *)destination_;
    cflow_test_owned_value *source = (cflow_test_owned_value *)source_;

    destination->resource = source->resource;
    source->resource = NULL;
}

static void cflow_test_owned_value_destroy(void *value_) {
    cflow_test_owned_value *value = (cflow_test_owned_value *)value_;

    free(value->resource);
    value->resource = NULL;
}

static const cmeta_type_traits cflow_test_owned_value_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = cflow_test_owned_value_copy,
    .move_construct = cflow_test_owned_value_move,
    .destroy = cflow_test_owned_value_destroy
};

const cmeta_type_desc cflow_test_owned_value_type = {
    .name = "cflow_test_owned_value",
    .size = sizeof(cflow_test_owned_value),
    .align = _Alignof(cflow_test_owned_value),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &cflow_test_owned_value_traits,
    .identity = NULL
};

typed(filter, value, bool, cflow_test_even, (int value)) {
    return value % 2 == 0;
}

typed(map, value, long, cflow_test_square, (int value)) {
    return (long)value * (long)value;
}

typed(map, value, double, cflow_test_half, (long value)) {
    return (double)value / 2.0;
}
