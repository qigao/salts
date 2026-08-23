#include <cbind/cbind.h>
#include "recording.h"
#include "tinytest.h"

#include <stddef.h>

#define CBIND_DATA_PREFIX_SIZE \
    (offsetof(cmeta_data_desc, shape) + sizeof(((cmeta_data_desc *)0)->shape))
#define TOKEN_MAP_BEGIN { .kind = CSERDE_MAP_BEGIN }
#define TOKEN_MAP_END { .kind = CSERDE_MAP_END }
#define TOKEN_SINT(v) { .kind = CSERDE_SINT, .value.sint = (v) }
#define TOKEN_KEY(text) \
    { .kind = CSERDE_STRING, \
      .value.slice = { (const unsigned char *)(text), sizeof(text) - 1u, \
                       CSERDE_VIEW_STABLE } }

Struct(cbind_attr_inner,
    (int, value)
);

Struct(cbind_attr_outer,
    (cbind_attr_inner, inner)
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

DEFINE_OBJECT_TYPE(cbind_attr_inner_type, cbind_attr_inner,
                   "test.cbind.attr.inner");
DEFINE_OBJECT_TYPE(cbind_attr_outer_type, cbind_attr_outer,
                   "test.cbind.attr.outer");

static const cmeta_data_field_desc inner_fields[] = {
    {"test.cbind.attr.inner.value", "value", offsetof(cbind_attr_inner, value),
     &cmeta_data_int}
};

static const cmeta_data_struct_shape inner_shape = {
    .layout = StructMeta(cbind_attr_inner),
    .fields = inner_fields,
    .field_count = 1u
};

static const cmeta_data_desc inner_data = {
    .struct_size = CBIND_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.attr.inner.data",
    .display_name = "attr-inner",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cbind_attr_inner_type,
    .shape = &inner_shape
};

static const cmeta_data_field_desc outer_fields[] = {
    {"test.cbind.attr.outer.inner", "inner", offsetof(cbind_attr_outer, inner),
     &inner_data}
};

static const cmeta_data_struct_shape outer_shape = {
    .layout = StructMeta(cbind_attr_outer),
    .fields = outer_fields,
    .field_count = 1u
};

static const cmeta_data_desc outer_data = {
    .struct_size = CBIND_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.attr.outer.data",
    .display_name = "attr-outer",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cbind_attr_outer_type,
    .shape = &outer_shape
};

static cbind_status decode(const cserde_token *tokens,
                           size_t token_count,
                           cbind_error *error,
                           size_t *source_index) {
    cserde_recording_reader_context source = {tokens, token_count, 0u};
    cserde_reader reader = {0};
    unsigned char scratch[2] = {0};
    cbind_context context = CBIND_CONTEXT_INIT(scratch, sizeof(scratch), 2u);
    cbind_attr_outer out = {0};
    cbind_status status;

    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops, &source),
                CSERDE_OK);
    status = cbind_decode(&context, &outer_data, &reader, &out, error);
    *source_index = source.index;
    return status;
}

spec("CBind nested struct error attribution") {
  it("keeps the resolved parent field for an unknown nested key") {
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN,
        TOKEN_KEY("inner"), TOKEN_MAP_BEGIN,
        TOKEN_KEY("missing"), TOKEN_SINT(7),
        TOKEN_MAP_END,
        TOKEN_MAP_END
    };
    cbind_error error = CBIND_ERROR_INIT;
    size_t source_index = 0u;

    check_equal(decode(tokens, sizeof(tokens) / sizeof(tokens[0]),
                       &error, &source_index),
                CBIND_UNKNOWN_FIELD);
    check_true(error.shape == &inner_data);
    check_true(error.field == &outer_fields[0]);
    check_equal(error.depth, (size_t)2u);
    check_equal(source_index, (size_t)4u);
  }

  it("keeps the resolved parent field for a non-string nested key") {
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN,
        TOKEN_KEY("inner"), TOKEN_MAP_BEGIN,
        TOKEN_SINT(1), TOKEN_SINT(7),
        TOKEN_MAP_END,
        TOKEN_MAP_END
    };
    cbind_error error = CBIND_ERROR_INIT;
    size_t source_index = 0u;

    check_equal(decode(tokens, sizeof(tokens) / sizeof(tokens[0]),
                       &error, &source_index),
                CBIND_TOKEN_MISMATCH);
    check_true(error.shape == &inner_data);
    check_true(error.field == &outer_fields[0]);
    check_equal(error.depth, (size_t)2u);
    check_equal(source_index, (size_t)4u);
  }
}

#undef DEFINE_OBJECT_TYPE
#undef TOKEN_KEY
#undef TOKEN_SINT
#undef TOKEN_MAP_END
#undef TOKEN_MAP_BEGIN
#undef CBIND_DATA_PREFIX_SIZE
