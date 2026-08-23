#include <cbind/cbind.h>
#include "recording.h"
#include "tinytest.h"

#include <stddef.h>

#define CBIND_DATA_PREFIX_SIZE \
    (offsetof(cmeta_data_desc, shape) + sizeof(((cmeta_data_desc *)0)->shape))

Struct(cbind_order_inner,
    (int, value)
);

Struct(cbind_order_outer,
    (cbind_order_inner, inner)
);

#define DEFINE_OBJECT_TYPE(symbol, ctype, stable_name) \
    static const cmeta_type_identity symbol##_identity = \
        CMETA_TYPE_ID_ATOM_INIT(stable_name); \
    static const cmeta_type_desc symbol = { \
        .name = #ctype, \
        .size = sizeof(ctype), \
        .align = _Alignof(ctype), \
        .kind = CMETA_T_OBJECT, \
        .pointee = NULL, \
        .traits = NULL, \
        .identity = &symbol##_identity \
    }

DEFINE_OBJECT_TYPE(cbind_order_inner_type, cbind_order_inner,
                   "test.cbind.order.inner");
DEFINE_OBJECT_TYPE(cbind_order_outer_type, cbind_order_outer,
                   "test.cbind.order.outer");

static const cmeta_data_field_desc cbind_order_inner_fields[] = {
    {"test.cbind.order.inner.value", "value", offsetof(cbind_order_inner, value),
     &cmeta_data_int}
};

static const cmeta_data_struct_shape cbind_order_inner_shape = {
    .layout = StructMeta(cbind_order_inner),
    .fields = cbind_order_inner_fields,
    .field_count = 1u
};

static const cmeta_data_field_desc cbind_order_outer_fields[] = {
    {"test.cbind.order.outer.inner", "inner", offsetof(cbind_order_outer, inner),
     NULL}
};

static const cmeta_data_struct_shape cbind_order_outer_shape = {
    .layout = StructMeta(cbind_order_outer),
    .fields = cbind_order_outer_fields,
    .field_count = 1u
};

static cbind_status decode_with_child(const cmeta_data_desc *child,
                                      size_t max_depth,
                                      size_t *source_index) {
    const cserde_token token = {.kind = CSERDE_MAP_BEGIN};
    cserde_recording_reader_context source = {&token, 1u, 0u};
    cserde_reader reader = {0};
    unsigned char scratch[2] = {0};
    cbind_context context = CBIND_CONTEXT_INIT(scratch, sizeof(scratch), max_depth);
    cbind_error error = CBIND_ERROR_INIT;
    cbind_order_outer out = {0};
    cmeta_data_field_desc outer_field = cbind_order_outer_fields[0];
    cmeta_data_struct_shape outer_shape = cbind_order_outer_shape;
    cmeta_data_desc outer_data = {
        .struct_size = CBIND_DATA_PREFIX_SIZE,
        .abi_version = CMETA_DATA_DESC_ABI_VERSION,
        .stable_id = "test.cbind.order.outer.data",
        .display_name = "order-outer",
        .kind = CMETA_DATA_STRUCT,
        .storage_type = &cbind_order_outer_type,
        .shape = &outer_shape
    };
    cbind_status status;

    outer_field.value = child;
    outer_shape.fields = &outer_field;
    check_true(cmeta_data_desc_valid(&outer_data));
    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops, &source),
                CSERDE_OK);
    status = cbind_decode(&context, &outer_data, &reader, &out, &error);
    *source_index = source.index;
    return status;
}

spec("CBind struct preflight ordering") {
  it("rejects a malformed nested shape before applying the depth budget") {
    cmeta_type_desc malformed_type = cbind_order_inner_type;
    cmeta_data_desc malformed_child = {
        .struct_size = CBIND_DATA_PREFIX_SIZE,
        .abi_version = CMETA_DATA_DESC_ABI_VERSION,
        .stable_id = "test.cbind.order.inner.data",
        .display_name = "order-inner",
        .kind = CMETA_DATA_STRUCT,
        .storage_type = &malformed_type,
        .shape = &cbind_order_inner_shape
    };
    size_t source_index = (size_t)-1;

    malformed_type.size += 1u;
    check_true(cmeta_data_desc_valid(&malformed_child));
    check_equal(decode_with_child(&malformed_child, 1u, &source_index),
                CBIND_INVALID_SHAPE);
    check_equal(source_index, (size_t)0u);
  }
}

#undef DEFINE_OBJECT_TYPE
#undef CBIND_DATA_PREFIX_SIZE
