#include <cbind/cbind.h>
#include <turbostl/typed.h>
#include "turbo_cmeta_data.h"
#include "turbo_str.h"
#include "recording.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

Struct(cbind_variant_pair,
    (int, value),
    (TYPE(Vec, int), items)
);

Enum(cbind_variant_tag,
    (CBIND_VARIANT_NUMBER, 1, "number"),
    (CBIND_VARIANT_PAIR, 2, "pair"),
    (CBIND_VARIANT_TEXT, 3, "text")
);

typedef struct cbind_variant_value {
    int tag;
    union {
        int number;
        cbind_variant_pair pair;
        tstr text;
    } payload;
} cbind_variant_value;

typedef enum cbind_variant_select_mode {
    CBIND_VARIANT_SELECT_OK,
    CBIND_VARIANT_SELECT_FAIL,
    CBIND_VARIANT_SELECT_WRONG_TAG,
    CBIND_VARIANT_SELECT_DIRTY_PAYLOAD
} cbind_variant_select_mode;

#define DEFINE_OBJECT_TYPE(symbol_, ctype_, stable_id_)                 \
    static const cmeta_type_identity symbol_##_identity =               \
        CMETA_TYPE_ID_ATOM_INIT(stable_id_);                            \
    static const cmeta_type_desc symbol_ = {                            \
        .name = #ctype_, .size = sizeof(ctype_),                        \
        .align = _Alignof(ctype_), .kind = CMETA_T_OBJECT,              \
        .pointee = NULL, .traits = NULL,                                \
        .identity = &symbol_##_identity}

DEFINE_OBJECT_TYPE(cbind_variant_pair_type, cbind_variant_pair,
                   "test.cbind.variant.Pair");
DEFINE_OBJECT_TYPE(cbind_variant_value_type, cbind_variant_value,
                   "test.cbind.variant.Value");

static const cmeta_type_identity cbind_variant_tag_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.cbind.variant.Tag");
static const cmeta_type_desc cbind_variant_tag_type = {
    .name = "cbind_variant_tag",
    .size = sizeof(cbind_variant_tag),
    .align = _Alignof(cbind_variant_tag),
    .kind = CMETA_T_INTEGER,
    .pointee = NULL,
    .traits = NULL,
    .identity = &cbind_variant_tag_identity
};

static const cmeta_data_field_desc cbind_variant_pair_fields[] = {
    {"test.cbind.variant.Pair.value", "value",
     offsetof(cbind_variant_pair, value), &cmeta_data_int},
    {"test.cbind.variant.Pair.items", "items",
     offsetof(cbind_variant_pair, items), &cmeta_data_sequence}
};
static const cmeta_data_struct_shape cbind_variant_pair_shape = {
    .layout = StructMeta(cbind_variant_pair),
    .fields = cbind_variant_pair_fields,
    .field_count = 2u
};
static const cmeta_data_desc cbind_variant_pair_data = {
    .struct_size = offsetof(cmeta_data_desc, shape) +
                   sizeof(((cmeta_data_desc *)0)->shape),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.variant.Pair.data",
    .display_name = "Pair",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cbind_variant_pair_type,
    .shape = &cbind_variant_pair_shape
};

static cbind_variant_select_mode cbind_variant_mode;

static bool cbind_variant_is_zero(const void *object) {
    const cbind_variant_value *value = (const cbind_variant_value *)object;
    return value != NULL && value->tag == 0;
}

static cmeta_status cbind_variant_active_tag(const void *object,
                                             int64_t *out) {
    const cbind_variant_value *value = (const cbind_variant_value *)object;
    if (value == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    *out = value->tag;
    return CMETA_OK;
}

static cmeta_status cbind_variant_select(void *object, int64_t tag) {
    cbind_variant_value *value = (cbind_variant_value *)object;
    if (value == NULL)
        return CMETA_INVALID_ARGUMENT;
    memset(&value->payload, 0, sizeof(value->payload));
    value->tag = (int)tag;
    if (cbind_variant_mode == CBIND_VARIANT_SELECT_WRONG_TAG)
        value->tag += 1;
    if (cbind_variant_mode == CBIND_VARIANT_SELECT_DIRTY_PAYLOAD)
        value->payload.number = 9;
    return cbind_variant_mode == CBIND_VARIANT_SELECT_FAIL
               ? CMETA_CALLBACK_ERROR
               : CMETA_OK;
}

static void cbind_variant_restore_zero(void *object) {
    cbind_variant_value *value = (cbind_variant_value *)object;
    if (value == NULL)
        return;
    if (value->tag == CBIND_VARIANT_PAIR) {
        const cmeta_field_desc *items = cmeta_struct_find_field(
            StructMeta(cbind_variant_pair), "items");
        (void)cmeta_container_restore_zero(
            &value->payload.pair.items, items->declared_type);
    } else if (value->tag == CBIND_VARIANT_TEXT) {
        turbo_tstr_cmeta_restore_zero(&value->payload.text);
    }
    memset(value, 0, sizeof(*value));
}

static const cmeta_data_buffer_shape cbind_variant_text_shape = {
    .ownership = CMETA_DATA_BUFFER_OWNED
};
static const cmeta_data_desc cbind_variant_text_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.variant.text.data",
    .display_name = "text",
    .kind = CMETA_DATA_STRING,
    .storage_type = &turbo_tstr_cmeta_type,
    .shape = &cbind_variant_text_shape,
    .buffer_ops = &turbo_tstr_cmeta_buffer_ops
};

static const cmeta_data_variant_case cbind_variant_cases[] = {
    {CBIND_VARIANT_NUMBER, "test.cbind.variant.number", "number",
     offsetof(cbind_variant_value, payload), &cmeta_data_int},
    {CBIND_VARIANT_PAIR, "test.cbind.variant.pair", "pair",
     offsetof(cbind_variant_value, payload), &cbind_variant_pair_data},
    {CBIND_VARIANT_TEXT, "test.cbind.variant.text", "text",
     offsetof(cbind_variant_value, payload), &cbind_variant_text_data}
};
static const cmeta_data_variant_shape cbind_variant_shape = {
    .tag_offset = offsetof(cbind_variant_value, tag),
    .tag = &cmeta_data_int,
    .cases = cbind_variant_cases,
    .case_count = 3u
};
static const cmeta_data_variant_ops cbind_variant_ops = {
    .struct_size = sizeof(cmeta_data_variant_ops),
    .abi_version = CMETA_DATA_VARIANT_OPS_ABI_VERSION,
    .storage_type = &cbind_variant_value_type,
    .is_zero = cbind_variant_is_zero,
    .active_tag = cbind_variant_active_tag,
    .select = cbind_variant_select,
    .restore_zero = cbind_variant_restore_zero
};
static const cmeta_data_desc cbind_variant_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.variant.Value.data",
    .display_name = "Value",
    .kind = CMETA_DATA_VARIANT,
    .storage_type = &cbind_variant_value_type,
    .shape = &cbind_variant_shape,
    .variant_ops = &cbind_variant_ops
};

typedef struct cbind_outer_variant_value {
    int tag;
    union {
        cbind_variant_value inner;
    } payload;
} cbind_outer_variant_value;

DEFINE_OBJECT_TYPE(cbind_outer_variant_value_type, cbind_outer_variant_value,
                   "test.cbind.variant.Outer");

static bool cbind_outer_variant_is_zero(const void *object) {
    const cbind_outer_variant_value *value =
        (const cbind_outer_variant_value *)object;
    return value != NULL && value->tag == 0;
}

static cmeta_status cbind_outer_variant_active_tag(const void *object,
                                                   int64_t *out) {
    const cbind_outer_variant_value *value =
        (const cbind_outer_variant_value *)object;
    if (value == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    *out = value->tag;
    return CMETA_OK;
}

static cmeta_status cbind_outer_variant_select(void *object, int64_t tag) {
    cbind_outer_variant_value *value = (cbind_outer_variant_value *)object;
    if (value == NULL)
        return CMETA_INVALID_ARGUMENT;
    memset(&value->payload, 0, sizeof(value->payload));
    value->tag = (int)tag;
    return CMETA_OK;
}

static void cbind_outer_variant_restore_zero(void *object) {
    cbind_outer_variant_value *value = (cbind_outer_variant_value *)object;
    if (value == NULL)
        return;
    if (value->tag == 10)
        (void)cmeta_data_variant_restore_zero(
            &cbind_variant_data, &value->payload.inner);
    memset(value, 0, sizeof(*value));
}

static const cmeta_data_variant_case cbind_outer_variant_cases[] = {
    {10, "test.cbind.variant.outer.inner", "inner",
     offsetof(cbind_outer_variant_value, payload), &cbind_variant_data}
};
static const cmeta_data_variant_shape cbind_outer_variant_shape = {
    .tag_offset = offsetof(cbind_outer_variant_value, tag),
    .tag = &cmeta_data_int,
    .cases = cbind_outer_variant_cases,
    .case_count = 1u
};
static const cmeta_data_variant_ops cbind_outer_variant_ops = {
    .struct_size = sizeof(cmeta_data_variant_ops),
    .abi_version = CMETA_DATA_VARIANT_OPS_ABI_VERSION,
    .storage_type = &cbind_outer_variant_value_type,
    .is_zero = cbind_outer_variant_is_zero,
    .active_tag = cbind_outer_variant_active_tag,
    .select = cbind_outer_variant_select,
    .restore_zero = cbind_outer_variant_restore_zero
};
static const cmeta_data_desc cbind_outer_variant_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.variant.Outer.data",
    .display_name = "Outer",
    .kind = CMETA_DATA_VARIANT,
    .storage_type = &cbind_outer_variant_value_type,
    .shape = &cbind_outer_variant_shape,
    .variant_ops = &cbind_outer_variant_ops
};

static const cmeta_data_enum_shape cbind_variant_enum_tag_shape = {
    .meta = EnumMeta(cbind_variant_tag)
};
static const cmeta_data_desc cbind_variant_enum_tag_data = {
    .struct_size = offsetof(cmeta_data_desc, shape) +
                   sizeof(((cmeta_data_desc *)0)->shape),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.variant.Tag.data",
    .display_name = "Tag",
    .kind = CMETA_DATA_ENUM,
    .storage_type = &cbind_variant_tag_type,
    .shape = &cbind_variant_enum_tag_shape
};

static cbind_status decode_tokens(const cmeta_data_desc *shape,
                                  const cserde_token *tokens,
                                  size_t token_count,
                                  void *out,
                                  size_t max_depth,
                                  size_t scratch_size,
                                  size_t *consumed,
                                  cbind_error *error) {
    unsigned char scratch[8] = {0};
    cserde_recording_reader_context source = {tokens, token_count, 0u};
    cserde_reader reader = {0};
    cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
        scratch_size == 0u ? NULL : scratch, scratch_size, max_depth,
        4u, 64u);
    cbind_status status;

    check(scratch_size <= sizeof(scratch));
    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops,
                                   &source), CSERDE_OK);
    status = cbind_decode(&context, shape, &reader, out, error);
    if (consumed != NULL)
        *consumed = source.index;
    return status;
}

spec("CBind variant preflight") {
  before_each() {
    cbind_variant_mode = CBIND_VARIANT_SELECT_OK;
  }

  it("rejects missing lifecycle ops and bad offsets before input") {
    const cserde_token tokens[] = {{.kind = CSERDE_ARRAY_BEGIN}};
    cmeta_data_desc missing_ops = cbind_variant_data;
    cmeta_data_variant_case bad_case = cbind_variant_cases[0];
    cmeta_data_variant_shape bad_shape = cbind_variant_shape;
    cmeta_data_desc bad_data = cbind_variant_data;
    cbind_variant_value out = {0};
    size_t consumed = 99u;

    missing_ops.struct_size = offsetof(cmeta_data_desc, shape) +
                              sizeof(missing_ops.shape);
    missing_ops.variant_ops = NULL;
    check_equal(decode_tokens(&missing_ops, tokens, 1u, &out, 2u, 1u,
                              &consumed, NULL), CBIND_UNSUPPORTED);
    check_equal(consumed, (size_t)0u);

    bad_case.offset = sizeof(cbind_variant_value);
    bad_shape.cases = &bad_case;
    bad_shape.case_count = 1u;
    bad_data.shape = &bad_shape;
    check_equal(decode_tokens(&bad_data, tokens, 1u, &out, 2u, 1u,
                              &consumed, NULL), CBIND_INVALID_SHAPE);
    check_equal(consumed, (size_t)0u);
  }

  it("rejects direct container cases and recursive variant cycles") {
    const cserde_token tokens[] = {{.kind = CSERDE_ARRAY_BEGIN}};
    cmeta_data_variant_case item = cbind_variant_cases[0];
    cmeta_data_variant_shape shape = cbind_variant_shape;
    cmeta_data_desc data = cbind_variant_data;
    cbind_variant_value out = {0};
    size_t consumed = 99u;

    item.value = &cmeta_data_sequence;
    shape.cases = &item;
    shape.case_count = 1u;
    data.shape = &shape;
    check_equal(decode_tokens(&data, tokens, 1u, &out, 2u, 1u,
                              &consumed, NULL), CBIND_UNSUPPORTED);
    check_equal(consumed, (size_t)0u);

    item.value = &data;
    check_equal(decode_tokens(&data, tokens, 1u, &out, 2u, 1u,
                              &consumed, NULL), CBIND_INVALID_SHAPE);
    check_equal(consumed, (size_t)0u);
  }

  it("accounts for aggregate depth and nested struct bitmap scratch") {
    const cserde_token tokens[] = {{.kind = CSERDE_ARRAY_BEGIN}};
    cbind_variant_value out = {0};
    size_t consumed = 99u;

    check_equal(decode_tokens(&cbind_variant_data, tokens, 1u, &out,
                              1u, 1u, &consumed, NULL),
                CBIND_LIMIT_EXCEEDED);
    check_equal(consumed, (size_t)0u);
    check_equal(decode_tokens(&cbind_variant_data, tokens, 1u, &out,
                              2u, 0u, &consumed, NULL),
                CBIND_LIMIT_EXCEEDED);
    check_equal(consumed, (size_t)0u);
  }

  it("stops shape traversal at max depth before inspecting deeper nodes") {
    enum { CBIND_DEEP_VARIANT_COUNT = 256 };
    const cserde_token tokens[] = {{.kind = CSERDE_ARRAY_BEGIN}};
    cmeta_data_desc data[CBIND_DEEP_VARIANT_COUNT];
    cmeta_data_variant_shape shapes[CBIND_DEEP_VARIANT_COUNT];
    cmeta_data_variant_case cases[CBIND_DEEP_VARIANT_COUNT];
    cbind_variant_value out = {0};
    size_t consumed = 99u;
    size_t i;

    for (i = 0u; i < CBIND_DEEP_VARIANT_COUNT; ++i) {
        data[i] = cbind_variant_data;
        shapes[i] = cbind_variant_shape;
        cases[i] = cbind_variant_cases[0];
        cases[i].offset = 0u;
        cases[i].value = i + 1u < CBIND_DEEP_VARIANT_COUNT
                             ? &data[i + 1u]
                             : &cmeta_data_sequence;
        shapes[i].cases = &cases[i];
        shapes[i].case_count = 1u;
        data[i].shape = &shapes[i];
    }

    check_equal(decode_tokens(&data[0], tokens, 1u, &out, 2u, 1u,
                              &consumed, NULL),
                CBIND_LIMIT_EXCEEDED);
    check_equal(consumed, (size_t)0u);
  }

  it("rejects under-aligned tag and payload storage before input") {
    const cserde_token tokens[] = {{.kind = CSERDE_ARRAY_BEGIN}};
    cmeta_data_variant_case bad_case = cbind_variant_cases[0];
    cmeta_data_variant_shape bad_shape = cbind_variant_shape;
    cmeta_data_desc bad_data = cbind_variant_data;
    cmeta_type_desc under_aligned_type = cbind_variant_value_type;
    cmeta_data_variant_ops under_aligned_ops = cbind_variant_ops;
    cbind_variant_value out = {0};
    size_t consumed = 99u;

    bad_shape.tag_offset = 1u;
    bad_data.shape = &bad_shape;
    check_equal(decode_tokens(&bad_data, tokens, 1u, &out, 2u, 1u,
                              &consumed, NULL), CBIND_INVALID_SHAPE);
    check_equal(consumed, (size_t)0u);

    bad_shape = cbind_variant_shape;
    bad_case.offset = 1u;
    bad_shape.cases = &bad_case;
    bad_shape.case_count = 1u;
    bad_data.shape = &bad_shape;
    check_equal(decode_tokens(&bad_data, tokens, 1u, &out, 2u, 1u,
                              &consumed, NULL), CBIND_INVALID_SHAPE);
    check_equal(consumed, (size_t)0u);

    under_aligned_type.align = 1u;
    under_aligned_ops.storage_type = &under_aligned_type;
    bad_data = cbind_variant_data;
    bad_data.storage_type = &under_aligned_type;
    bad_data.variant_ops = &under_aligned_ops;
    check_equal(decode_tokens(&bad_data, tokens, 1u, &out, 2u, 1u,
                              &consumed, NULL), CBIND_INVALID_SHAPE);
    check_equal(consumed, (size_t)0u);
  }
}

spec("CBind variant decode") {
  before_each() {
    cbind_variant_mode = CBIND_VARIANT_SELECT_OK;
  }

  it("decodes scalar and struct cases with the exact array grammar") {
    static const unsigned char value_key[] = {'v', 'a', 'l', 'u', 'e'};
    static const unsigned char items_key[] = {'i', 't', 'e', 'm', 's'};
    const cserde_token scalar_tokens[] = {
        {.kind = CSERDE_ARRAY_BEGIN},
        {.kind = CSERDE_SINT, .value.sint = CBIND_VARIANT_NUMBER},
        {.kind = CSERDE_SINT, .value.sint = 42},
        {.kind = CSERDE_ARRAY_END}
    };
    const cserde_token struct_tokens[] = {
        {.kind = CSERDE_ARRAY_BEGIN},
        {.kind = CSERDE_SINT, .value.sint = CBIND_VARIANT_PAIR},
        {.kind = CSERDE_MAP_BEGIN},
        {.kind = CSERDE_STRING,
         .value.slice = {value_key, sizeof(value_key), CSERDE_VIEW_TRANSIENT}},
        {.kind = CSERDE_SINT, .value.sint = 7},
        {.kind = CSERDE_STRING,
         .value.slice = {items_key, sizeof(items_key), CSERDE_VIEW_STABLE}},
        {.kind = CSERDE_ARRAY_BEGIN},
        {.kind = CSERDE_SINT, .value.sint = 4},
        {.kind = CSERDE_SINT, .value.sint = 6},
        {.kind = CSERDE_ARRAY_END},
        {.kind = CSERDE_MAP_END},
        {.kind = CSERDE_ARRAY_END}
    };
    cbind_variant_value out = {0};
    const cbind_variant_value zero = {0};

    check_equal(decode_tokens(&cbind_variant_data, scalar_tokens, 4u, &out,
                              2u, 1u, NULL, NULL), CBIND_OK);
    check_equal(out.tag, CBIND_VARIANT_NUMBER);
    check_equal(out.payload.number, 42);

    cbind_variant_restore_zero(&out);
    check_equal(decode_tokens(&cbind_variant_data, struct_tokens, 12u, &out,
                              2u, 1u, NULL, NULL), CBIND_OK);
    check_equal(out.tag, CBIND_VARIANT_PAIR);
    check_equal(out.payload.pair.value, 7);
    check_equal(vec_size(&out.payload.pair.items), (size_t)2u);
    check_equal(*(const int *)vec_at_const(&out.payload.pair.items, 0u), 4);
    check_equal(*(const int *)vec_at_const(&out.payload.pair.items, 1u), 6);
    cbind_variant_restore_zero(&out);
    check_equal(memcmp(&out, &zero, sizeof(out)), 0);
  }

  it("decodes an enum tag by reflected text without enum storage ops") {
    static const unsigned char number_text[] = {'n', 'u', 'm', 'b', 'e', 'r'};
    const cserde_token tokens[] = {
        {.kind = CSERDE_ARRAY_BEGIN},
        {.kind = CSERDE_STRING,
         .value.slice = {number_text, sizeof(number_text),
                         CSERDE_VIEW_STABLE}},
        {.kind = CSERDE_SINT, .value.sint = 5},
        {.kind = CSERDE_ARRAY_END}
    };
    cmeta_data_variant_shape shape = cbind_variant_shape;
    cmeta_data_desc data = cbind_variant_data;
    cbind_variant_value out = {0};

    shape.tag = &cbind_variant_enum_tag_data;
    data.shape = &shape;
    check_equal(decode_tokens(&data, tokens, 4u, &out, 2u, 1u, NULL, NULL),
                CBIND_OK);
    check_equal(out.tag, CBIND_VARIANT_NUMBER);
    check_equal(out.payload.number, 5);
    cbind_variant_restore_zero(&out);
  }

  it("decodes owned buffer payloads and releases them through variant reset") {
    static const unsigned char text[] = {'h', 'e', 'l', 'l', 'o'};
    const cserde_token tokens[] = {
        {.kind = CSERDE_ARRAY_BEGIN},
        {.kind = CSERDE_SINT, .value.sint = CBIND_VARIANT_TEXT},
        {.kind = CSERDE_STRING,
         .value.slice = {text, sizeof(text), CSERDE_VIEW_TRANSIENT}},
        {.kind = CSERDE_ARRAY_END}
    };
    cbind_variant_value out = {0};

    check_equal(decode_tokens(&cbind_variant_data, tokens, 4u, &out,
                              2u, 1u, NULL, NULL), CBIND_OK);
    check_equal(out.tag, CBIND_VARIANT_TEXT);
    check_not_null(out.payload.text);
    check_equal(tstr_len(out.payload.text), sizeof(text));
    check_equal(out.payload.text, "hello");
    cbind_variant_restore_zero(&out);
    check_true(cbind_variant_is_zero(&out));
    check_null(out.payload.text);
  }

  it("decodes nested variants and restores the complete active chain") {
    const cserde_token tokens[] = {
        {.kind = CSERDE_ARRAY_BEGIN},
        {.kind = CSERDE_SINT, .value.sint = 10},
        {.kind = CSERDE_ARRAY_BEGIN},
        {.kind = CSERDE_SINT, .value.sint = CBIND_VARIANT_NUMBER},
        {.kind = CSERDE_SINT, .value.sint = 23},
        {.kind = CSERDE_ARRAY_END},
        {.kind = CSERDE_ARRAY_END}
    };
    cbind_outer_variant_value out = {0};

    check_equal(decode_tokens(&cbind_outer_variant_data, tokens, 7u, &out,
                              3u, 1u, NULL, NULL), CBIND_OK);
    check_equal(out.tag, 10);
    check_equal(out.payload.inner.tag, CBIND_VARIANT_NUMBER);
    check_equal(out.payload.inner.payload.number, 23);
    cbind_outer_variant_restore_zero(&out);
    check_true(cbind_outer_variant_is_zero(&out));
    check_true(cbind_variant_is_zero(&out.payload.inner));
  }

  it("rejects unknown tags and malformed array lengths with rollback") {
    static const unsigned char owned_text[] = {'o', 'w', 'n', 'e', 'd'};
    const cserde_token wrong_begin[] = {
        {.kind = CSERDE_MAP_BEGIN}
    };
    const cserde_token missing_tag[] = {
        {.kind = CSERDE_ARRAY_BEGIN}
    };
    const cserde_token missing_payload[] = {
        {.kind = CSERDE_ARRAY_BEGIN},
        {.kind = CSERDE_SINT, .value.sint = CBIND_VARIANT_NUMBER}
    };
    const cserde_token unknown[] = {
        {.kind = CSERDE_ARRAY_BEGIN},
        {.kind = CSERDE_SINT, .value.sint = 99},
        {.kind = CSERDE_SINT, .value.sint = 1},
        {.kind = CSERDE_ARRAY_END}
    };
    const cserde_token extra[] = {
        {.kind = CSERDE_ARRAY_BEGIN},
        {.kind = CSERDE_SINT, .value.sint = CBIND_VARIANT_NUMBER},
        {.kind = CSERDE_SINT, .value.sint = 1},
        {.kind = CSERDE_SINT, .value.sint = 2},
        {.kind = CSERDE_ARRAY_END}
    };
    const cserde_token missing_end[] = {
        {.kind = CSERDE_ARRAY_BEGIN},
        {.kind = CSERDE_SINT, .value.sint = CBIND_VARIANT_NUMBER},
        {.kind = CSERDE_SINT, .value.sint = 1}
    };
    const cserde_token owned_missing_end[] = {
        {.kind = CSERDE_ARRAY_BEGIN},
        {.kind = CSERDE_SINT, .value.sint = CBIND_VARIANT_TEXT},
        {.kind = CSERDE_STRING,
         .value.slice = {owned_text, sizeof(owned_text),
                         CSERDE_VIEW_TRANSIENT}}
    };
    cbind_variant_value out = {0};

    check_equal(decode_tokens(&cbind_variant_data, wrong_begin, 1u, &out,
                              2u, 1u, NULL, NULL), CBIND_TOKEN_MISMATCH);
    check_true(cbind_variant_is_zero(&out));
    check_equal(decode_tokens(&cbind_variant_data, missing_tag, 1u, &out,
                              2u, 1u, NULL, NULL), CBIND_UNEXPECTED_END);
    check_true(cbind_variant_is_zero(&out));
    check_equal(decode_tokens(&cbind_variant_data, missing_payload, 2u, &out,
                              2u, 1u, NULL, NULL), CBIND_UNEXPECTED_END);
    check_true(cbind_variant_is_zero(&out));
    check_equal(decode_tokens(&cbind_variant_data, unknown, 4u, &out,
                              2u, 1u, NULL, NULL),
                CBIND_VALUE_OUT_OF_RANGE);
    check_true(cbind_variant_is_zero(&out));
    check_equal(decode_tokens(&cbind_variant_data, extra, 5u, &out,
                              2u, 1u, NULL, NULL),
                CBIND_TOKEN_MISMATCH);
    check_true(cbind_variant_is_zero(&out));
    check_equal(decode_tokens(&cbind_variant_data, missing_end, 3u, &out,
                              2u, 1u, NULL, NULL),
                CBIND_UNEXPECTED_END);
    check_true(cbind_variant_is_zero(&out));
    check_equal(decode_tokens(&cbind_variant_data, owned_missing_end, 3u, &out,
                              2u, 1u, NULL, NULL),
                CBIND_UNEXPECTED_END);
    check_true(cbind_variant_is_zero(&out));
    check_null(out.payload.text);
  }

  it("rolls back select postcondition and payload failures") {
    static const unsigned char bad_payload[] = {'x'};
    const cserde_token tokens[] = {
        {.kind = CSERDE_ARRAY_BEGIN},
        {.kind = CSERDE_SINT, .value.sint = CBIND_VARIANT_NUMBER},
        {.kind = CSERDE_SINT, .value.sint = 8},
        {.kind = CSERDE_ARRAY_END}
    };
    cserde_token payload_mismatch[4];
    cbind_variant_value out = {0};
    cbind_error error = CBIND_ERROR_INIT;

    cbind_variant_mode = CBIND_VARIANT_SELECT_FAIL;
    check_equal(decode_tokens(&cbind_variant_data, tokens, 4u, &out,
                              2u, 1u, NULL, &error), CBIND_TARGET_ERROR);
    check_equal(error.target_status, CMETA_CALLBACK_ERROR);
    check_true(cbind_variant_is_zero(&out));

    cbind_variant_mode = CBIND_VARIANT_SELECT_DIRTY_PAYLOAD;
    error = (cbind_error)CBIND_ERROR_INIT;
    check_equal(decode_tokens(&cbind_variant_data, tokens, 4u, &out,
                              2u, 1u, NULL, &error), CBIND_TARGET_ERROR);
    check_equal(error.target_status, CMETA_CALLBACK_ERROR);
    check_true(cbind_variant_is_zero(&out));

    cbind_variant_mode = CBIND_VARIANT_SELECT_OK;
    memcpy(payload_mismatch, tokens, sizeof(tokens));
    payload_mismatch[2] = (cserde_token){
        .kind = CSERDE_STRING,
        .value.slice = {bad_payload, sizeof(bad_payload), CSERDE_VIEW_STABLE}};
    check_equal(decode_tokens(&cbind_variant_data, payload_mismatch, 4u, &out,
                              2u, 1u, NULL, NULL), CBIND_TOKEN_MISMATCH);
    check_true(cbind_variant_is_zero(&out));
  }

  it("rejects a selected destination before consuming input") {
    const cserde_token tokens[] = {{.kind = CSERDE_ARRAY_BEGIN}};
    cbind_variant_value out = {0};
    size_t consumed = 99u;

    check_equal(cmeta_data_variant_select(&cbind_variant_data, &out,
                                          CBIND_VARIANT_NUMBER), CMETA_OK);
    check_equal(decode_tokens(&cbind_variant_data, tokens, 1u, &out,
                              2u, 1u, &consumed, NULL),
                CBIND_DESTINATION_NOT_EMPTY);
    check_equal(consumed, (size_t)0u);
    check_equal(out.tag, CBIND_VARIANT_NUMBER);
    cbind_variant_restore_zero(&out);
  }
}

#undef DEFINE_OBJECT_TYPE
