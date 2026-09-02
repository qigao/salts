#include <cbind/cbind.h>
#include <rocida/stl/typed.h>
#include "recording.h"
#include "tinytest.h"

#include <stddef.h>
#include <string.h>

#define CBIND_DATA_PREFIX_SIZE \
    (offsetof(cmeta_data_desc, shape) + sizeof(((cmeta_data_desc *)0)->shape))
#define TOKEN_MAP_BEGIN { .kind = CSERDE_MAP_BEGIN }
#define TOKEN_MAP_END { .kind = CSERDE_MAP_END }
#define TOKEN_ARRAY_BEGIN { .kind = CSERDE_ARRAY_BEGIN }
#define TOKEN_ARRAY_END { .kind = CSERDE_ARRAY_END }
#define TOKEN_SINT(value_) { .kind = CSERDE_SINT, .value.sint = (value_) }
#define TOKEN_KEY(text_)                                                   \
    { .kind = CSERDE_STRING,                                               \
      .value.slice = {(const unsigned char *)(text_), sizeof(text_) - 1u,  \
                      CSERDE_VIEW_STABLE} }

Struct(cbind_container_payload,
    (TYPE(Vec, int), values),
    (TYPE(Set, int), unique),
    (TYPE(Map, int, long), index)
);

Struct(cbind_container_scalar_payload,
    (TYPE(Vec, bool), flags),
    (TYPE(Vec, long), longs),
    (TYPE(Vec, float), floats),
    (TYPE(Vec, double), doubles)
);

Struct(cbind_container_inner,
    (TYPE(Vec, double), samples)
);

Struct(cbind_container_outer,
    (int, id),
    (cbind_container_inner, inner)
);

static const cmeta_type_identity cbind_container_payload_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.cbind.container.payload");
static const cmeta_type_desc cbind_container_payload_type = {
    .name = "cbind_container_payload",
    .size = sizeof(cbind_container_payload),
    .align = _Alignof(cbind_container_payload),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = &cbind_container_payload_identity
};
static const cmeta_data_field_desc cbind_container_payload_fields[] = {
    {"test.cbind.container.values", "values",
     offsetof(cbind_container_payload, values), &cmeta_data_sequence},
    {"test.cbind.container.unique", "unique",
     offsetof(cbind_container_payload, unique), &cmeta_data_set},
    {"test.cbind.container.index", "index",
     offsetof(cbind_container_payload, index), &cmeta_data_map}
};
static const cmeta_data_struct_shape cbind_container_payload_shape = {
    .layout = StructMeta(cbind_container_payload),
    .fields = cbind_container_payload_fields,
    .field_count = 3u
};
static const cmeta_data_desc cbind_container_payload_data = {
    .struct_size = CBIND_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.container.payload.data",
    .display_name = "container payload",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cbind_container_payload_type,
    .shape = &cbind_container_payload_shape
};

#define DEFINE_OBJECT_DATA_TYPE(symbol_, ctype_, stable_id_)             \
    static const cmeta_type_identity symbol_##_identity =                \
        CMETA_TYPE_ID_ATOM_INIT(stable_id_);                             \
    static const cmeta_type_desc symbol_ = {                             \
        .name = #ctype_, .size = sizeof(ctype_),                         \
        .align = _Alignof(ctype_), .kind = CMETA_T_OBJECT,               \
        .pointee = NULL, .traits = NULL,                                 \
        .identity = &symbol_##_identity}

DEFINE_OBJECT_DATA_TYPE(cbind_container_scalar_payload_type,
                        cbind_container_scalar_payload,
                        "test.cbind.container.scalars");
DEFINE_OBJECT_DATA_TYPE(cbind_container_inner_type, cbind_container_inner,
                        "test.cbind.container.inner");
DEFINE_OBJECT_DATA_TYPE(cbind_container_outer_type, cbind_container_outer,
                        "test.cbind.container.outer");

static const cmeta_data_field_desc cbind_container_scalar_fields[] = {
    {"test.cbind.container.flags", "flags",
     offsetof(cbind_container_scalar_payload, flags), &cmeta_data_sequence},
    {"test.cbind.container.longs", "longs",
     offsetof(cbind_container_scalar_payload, longs), &cmeta_data_sequence},
    {"test.cbind.container.floats", "floats",
     offsetof(cbind_container_scalar_payload, floats), &cmeta_data_sequence},
    {"test.cbind.container.doubles", "doubles",
     offsetof(cbind_container_scalar_payload, doubles), &cmeta_data_sequence}
};
static const cmeta_data_struct_shape cbind_container_scalar_shape = {
    StructMeta(cbind_container_scalar_payload),
    cbind_container_scalar_fields,
    4u
};
static const cmeta_data_desc cbind_container_scalar_data = {
    CBIND_DATA_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "test.cbind.container.scalars.data", "container scalars",
    CMETA_DATA_STRUCT, &cbind_container_scalar_payload_type,
    &cbind_container_scalar_shape
};

static const cmeta_data_field_desc cbind_container_inner_fields[] = {
    {"test.cbind.container.inner.samples", "samples",
     offsetof(cbind_container_inner, samples), &cmeta_data_sequence}
};
static const cmeta_data_struct_shape cbind_container_inner_shape = {
    StructMeta(cbind_container_inner), cbind_container_inner_fields, 1u
};
static const cmeta_data_desc cbind_container_inner_data = {
    CBIND_DATA_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "test.cbind.container.inner.data", "container inner",
    CMETA_DATA_STRUCT, &cbind_container_inner_type,
    &cbind_container_inner_shape
};
static const cmeta_data_field_desc cbind_container_outer_fields[] = {
    {"test.cbind.container.outer.id", "id",
     offsetof(cbind_container_outer, id), &cmeta_data_int},
    {"test.cbind.container.outer.inner", "inner",
     offsetof(cbind_container_outer, inner), &cbind_container_inner_data}
};
static const cmeta_data_struct_shape cbind_container_outer_shape = {
    StructMeta(cbind_container_outer), cbind_container_outer_fields, 2u
};
static const cmeta_data_desc cbind_container_outer_data = {
    CBIND_DATA_PREFIX_SIZE, CMETA_DATA_DESC_ABI_VERSION,
    "test.cbind.container.outer.data", "container outer",
    CMETA_DATA_STRUCT, &cbind_container_outer_type,
    &cbind_container_outer_shape
};

#undef DEFINE_OBJECT_DATA_TYPE

static cbind_status decode_payload(const cserde_token *tokens,
                                   size_t token_count,
                                   size_t item_limit,
                                   cbind_container_payload *out,
                                   cbind_error *error,
                                   size_t *source_index) {
    unsigned char scratch[1] = {0};
    cserde_recording_reader_context source = {tokens, token_count, 0u};
    cserde_reader reader = {0};
    cbind_context context = CBIND_CONTEXT_WITH_CONTAINERS_INIT(
        scratch, sizeof(scratch), 1u, item_limit);
    cbind_status status;

    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops,
                                   &source), CSERDE_OK);
    status = cbind_decode(&context, &cbind_container_payload_data,
                          &reader, out, error);
    if (source_index != NULL)
        *source_index = source.index;
    return status;
}

spec("CBind scalar container decode") {
  it("decodes sequence, set, and map fields through declared collectors") {
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN,
        TOKEN_KEY("values"), TOKEN_ARRAY_BEGIN,
        TOKEN_SINT(2), TOKEN_SINT(5), TOKEN_ARRAY_END,
        TOKEN_KEY("unique"), TOKEN_ARRAY_BEGIN,
        TOKEN_SINT(7), TOKEN_SINT(9), TOKEN_ARRAY_END,
        TOKEN_KEY("index"), TOKEN_MAP_BEGIN,
        TOKEN_SINT(3), TOKEN_SINT(30),
        TOKEN_SINT(4), TOKEN_SINT(40), TOKEN_MAP_END,
        TOKEN_MAP_END
    };
    cbind_container_payload out = {0};
    cbind_error error = CBIND_ERROR_INIT;
    int seven = 7;
    int key = 4;
    const long *mapped;

    check_equal(decode_payload(tokens, sizeof(tokens) / sizeof(tokens[0]),
                               2u, &out, &error, NULL), CBIND_OK);
    check_equal(vec_size(&out.values), (size_t)2u);
    check_equal(*(const int *)vec_at_const(&out.values, 0u), 2);
    check_equal(*(const int *)vec_at_const(&out.values, 1u), 5);
    check_equal(set_size(&out.unique), (size_t)2u);
    check_true(set_contains(&out.unique, &seven));
    mapped = (const long *)map_get_const(&out.index, &key);
    check_not_null(mapped);
    check_equal(*mapped, 40L);
    check_equal(error.target_status, CMETA_OK);

    vec_destroy(&out.values);
    set_destroy(&out.unique);
    map_destroy(&out.index);
  }

  it("uses the D2 strict scalar conversions for every TYPE scalar family") {
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN,
        TOKEN_KEY("flags"), TOKEN_ARRAY_BEGIN,
        {.kind = CSERDE_BOOL, .value.boolean = true}, TOKEN_ARRAY_END,
        TOKEN_KEY("longs"), TOKEN_ARRAY_BEGIN,
        TOKEN_SINT(-17), TOKEN_ARRAY_END,
        TOKEN_KEY("floats"), TOKEN_ARRAY_BEGIN,
        {.kind = CSERDE_FLOAT, .value.floating = 1.5}, TOKEN_ARRAY_END,
        TOKEN_KEY("doubles"), TOKEN_ARRAY_BEGIN,
        {.kind = CSERDE_FLOAT, .value.floating = 2.25}, TOKEN_ARRAY_END,
        TOKEN_MAP_END
    };
    unsigned char scratch[1] = {0};
    cserde_recording_reader_context source = {
        tokens, sizeof(tokens) / sizeof(tokens[0]), 0u};
    cserde_reader reader = {0};
    cbind_context context = CBIND_CONTEXT_WITH_CONTAINERS_INIT(
        scratch, sizeof(scratch), 1u, 1u);
    cbind_container_scalar_payload out = {0};

    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops,
                                   &source), CSERDE_OK);
    check_equal(cbind_decode(&context, &cbind_container_scalar_data,
                             &reader, &out, NULL), CBIND_OK);
    check_true(*(const bool *)vec_at_const(&out.flags, 0u));
    check_equal(*(const long *)vec_at_const(&out.longs, 0u), -17L);
    check_equal(*(const float *)vec_at_const(&out.floats, 0u), 1.5f);
    check_equal(*(const double *)vec_at_const(&out.doubles, 0u), 2.25);
    vec_destroy(&out.flags);
    vec_destroy(&out.longs);
    vec_destroy(&out.floats);
    vec_destroy(&out.doubles);
  }

  it("decodes a container field inside a nested struct") {
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN,
        TOKEN_KEY("id"), TOKEN_SINT(8),
        TOKEN_KEY("inner"), TOKEN_MAP_BEGIN,
        TOKEN_KEY("samples"), TOKEN_ARRAY_BEGIN,
        {.kind = CSERDE_FLOAT, .value.floating = 3.5}, TOKEN_ARRAY_END,
        TOKEN_MAP_END,
        TOKEN_MAP_END
    };
    unsigned char scratch[2] = {0};
    cserde_recording_reader_context source = {
        tokens, sizeof(tokens) / sizeof(tokens[0]), 0u};
    cserde_reader reader = {0};
    cbind_context context = CBIND_CONTEXT_WITH_CONTAINERS_INIT(
        scratch, sizeof(scratch), 2u, 1u);
    cbind_container_outer out = {0};

    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops,
                                   &source), CSERDE_OK);
    check_equal(cbind_decode(&context, &cbind_container_outer_data,
                             &reader, &out, NULL), CBIND_OK);
    check_equal(out.id, 8);
    check_equal(vec_size(&out.inner.samples), (size_t)1u);
    check_equal(*(const double *)vec_at_const(&out.inner.samples, 0u), 3.5);
    vec_destroy(&out.inner.samples);
  }

  it("commits empty containers with a zero item budget") {
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN,
        TOKEN_KEY("values"), TOKEN_ARRAY_BEGIN, TOKEN_ARRAY_END,
        TOKEN_KEY("unique"), TOKEN_ARRAY_BEGIN, TOKEN_ARRAY_END,
        TOKEN_KEY("index"), TOKEN_MAP_BEGIN, TOKEN_MAP_END,
        TOKEN_MAP_END
    };
    cbind_container_payload out = {0};

    check_equal(decode_payload(tokens, sizeof(tokens) / sizeof(tokens[0]),
                               0u, &out, NULL, NULL), CBIND_OK);
    check_equal(vec_size(&out.values), (size_t)0u);
    check_equal(set_size(&out.unique), (size_t)0u);
    check_equal(map_size(&out.index), (size_t)0u);
    vec_destroy(&out.values);
    set_destroy(&out.unique);
    map_destroy(&out.index);
  }

  it("enforces a per-container item limit and restores the whole root") {
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN, TOKEN_KEY("values"), TOKEN_ARRAY_BEGIN,
        TOKEN_SINT(1), TOKEN_SINT(2), TOKEN_ARRAY_END
    };
    cbind_container_payload out = {0};
    cbind_container_payload zero = {0};
    cbind_error error = CBIND_ERROR_INIT;

    check_equal(decode_payload(tokens, sizeof(tokens) / sizeof(tokens[0]),
                               1u, &out, &error, NULL),
                CBIND_LIMIT_EXCEEDED);
    check_equal(error.target_status, CMETA_CAPACITY_EXCEEDED);
    check_equal(memcmp(&out, &zero, sizeof(out)), 0);
  }

  it("rolls back a committed earlier container when a later field fails") {
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN,
        TOKEN_KEY("values"), TOKEN_ARRAY_BEGIN,
        TOKEN_SINT(1), TOKEN_ARRAY_END,
        TOKEN_KEY("unique"), TOKEN_ARRAY_BEGIN,
        TOKEN_KEY("not-an-int")
    };
    cbind_container_payload out = {0};
    cbind_container_payload zero = {0};
    cbind_error error = CBIND_ERROR_INIT;

    check_equal(decode_payload(tokens, sizeof(tokens) / sizeof(tokens[0]),
                               4u, &out, &error, NULL),
                CBIND_TOKEN_MISMATCH);
    check_equal(memcmp(&out, &zero, sizeof(out)), 0);
  }

  it("requires the extended context before consuming container input") {
    const cserde_token token = TOKEN_MAP_BEGIN;
    unsigned char scratch[1] = {0};
    cserde_recording_reader_context source = {&token, 1u, 0u};
    cserde_reader reader = {0};
    cbind_context context = CBIND_CONTEXT_INIT(
        scratch, sizeof(scratch), 1u);
    cbind_container_payload out = {0};

    context.struct_size = offsetof(cbind_context, max_depth) +
                          sizeof(context.max_depth);
    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops,
                                   &source), CSERDE_OK);
    check_equal(cbind_decode(&context, &cbind_container_payload_data,
                             &reader, &out, NULL), CBIND_INVALID_CONTEXT);
    check_equal(source.index, (size_t)0u);
  }

  it("does not write target status through a legacy error prefix") {
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN, TOKEN_KEY("values"), TOKEN_ARRAY_BEGIN,
        TOKEN_SINT(1), TOKEN_SINT(2), TOKEN_ARRAY_END
    };
    cbind_container_payload out = {0};
    cbind_error error = CBIND_ERROR_INIT;

    error.struct_size = offsetof(cbind_error, depth) + sizeof(error.depth);
    error.target_status = CMETA_CALLBACK_ERROR;
    check_equal(decode_payload(tokens, sizeof(tokens) / sizeof(tokens[0]),
                               1u, &out, &error, NULL),
                CBIND_LIMIT_EXCEEDED);
    check_equal(error.target_status, CMETA_CALLBACK_ERROR);
  }

  it("rejects semantic/provider category mismatch before source input") {
    cmeta_data_field_desc fields[3];
    cmeta_data_struct_shape shape = cbind_container_payload_shape;
    cmeta_data_desc data = cbind_container_payload_data;
    cbind_container_payload out = {0};
    const cserde_token token = TOKEN_MAP_BEGIN;
    unsigned char scratch[1] = {0};
    cserde_recording_reader_context source = {&token, 1u, 0u};
    cserde_reader reader = {0};
    cbind_context context = CBIND_CONTEXT_WITH_CONTAINERS_INIT(
        scratch, sizeof(scratch), 1u, 1u);

    memcpy(fields, cbind_container_payload_fields, sizeof(fields));
    fields[0].value = &cmeta_data_set;
    shape.fields = fields;
    data.shape = &shape;
    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops,
                                   &source), CSERDE_OK);
    check_equal(cbind_decode(&context, &data, &reader, &out, NULL),
                CBIND_INVALID_SHAPE);
    check_equal(source.index, (size_t)0u);
  }

  it("rejects a provider without transactional restore before source input") {
    cmeta_field_desc reflected[3];
    cmeta_struct_desc layout = *StructMeta(cbind_container_payload);
    cmeta_data_struct_shape shape = cbind_container_payload_shape;
    cmeta_data_desc data = cbind_container_payload_data;
    cmeta_declared_type declared =
        *StructMeta(cbind_container_payload)->fields[0].declared_type;
    cmeta_container_construct_ops legacy = *declared.construction;
    cbind_container_payload out = {0};
    const cserde_token token = TOKEN_MAP_BEGIN;
    unsigned char scratch[1] = {0};
    cserde_recording_reader_context source = {&token, 1u, 0u};
    cserde_reader reader = {0};
    cbind_context context = CBIND_CONTEXT_WITH_CONTAINERS_INIT(
        scratch, sizeof(scratch), 1u, 1u);

    memcpy(reflected, StructMeta(cbind_container_payload)->fields,
           sizeof(reflected));
    legacy.struct_size = offsetof(cmeta_container_construct_ops, bind_types) +
                         sizeof(legacy.bind_types);
    declared.construction = &legacy;
    reflected[0].declared_type = &declared;
    layout.fields = reflected;
    shape.layout = &layout;
    data.shape = &shape;

    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops,
                                   &source), CSERDE_OK);
    check_equal(cbind_decode(&context, &data, &reader, &out, NULL),
                CBIND_UNSUPPORTED);
    check_equal(source.index, (size_t)0u);
  }

  it("rejects a bound container destination before source input") {
    const cmeta_field_desc *values =
        cmeta_struct_find_field(StructMeta(cbind_container_payload), "values");
    cbind_container_payload out = {0};
    const cserde_token token = TOKEN_MAP_BEGIN;
    unsigned char scratch[1] = {0};
    cserde_recording_reader_context source = {&token, 1u, 0u};
    cserde_reader reader = {0};
    cbind_context context = CBIND_CONTEXT_WITH_CONTAINERS_INIT(
        scratch, sizeof(scratch), 1u, 1u);

    check_equal(cmeta_container_bind_types(
                    &out.values, values->declared_type), CMETA_OK);
    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops,
                                   &source), CSERDE_OK);
    check_equal(cbind_decode(&context, &cbind_container_payload_data,
                             &reader, &out, NULL),
                CBIND_DESTINATION_NOT_EMPTY);
    check_equal(source.index, (size_t)0u);
    check_equal(cmeta_container_restore_zero(
                    &out.values, values->declared_type), CMETA_OK);
  }

  it("preserves an exact Collector target failure and restores the root") {
    static const cmeta_type_desc *const arguments[] = {&cmeta_type_size};
    cmeta_field_desc reflected[3];
    cmeta_struct_desc layout = *StructMeta(cbind_container_payload);
    cmeta_data_struct_shape shape = {
        .layout = &layout,
        .fields = &cbind_container_payload_fields[0],
        .field_count = 1u
    };
    cmeta_data_desc data = cbind_container_payload_data;
    cmeta_declared_type declared =
        *StructMeta(cbind_container_payload)->fields[0].declared_type;
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN, TOKEN_KEY("values")
    };
    unsigned char scratch[1] = {0};
    cserde_recording_reader_context source = {
        tokens, sizeof(tokens) / sizeof(tokens[0]), 0u};
    cserde_reader reader = {0};
    cbind_context context = CBIND_CONTEXT_WITH_CONTAINERS_INIT(
        scratch, sizeof(scratch), 1u, 1u);
    cbind_container_payload out = {0};
    cbind_container_payload zero = {0};
    cbind_error error = CBIND_ERROR_INIT;

    memcpy(reflected, StructMeta(cbind_container_payload)->fields,
           sizeof(reflected));
    declared.arguments = arguments;
    reflected[0].declared_type = &declared;
    layout.fields = reflected;
    data.shape = &shape;

    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops,
                                   &source), CSERDE_OK);
    check_equal(cbind_decode(&context, &data, &reader, &out, &error),
                CBIND_TARGET_ERROR);
    check_equal(error.target_status, CMETA_TRAIT_MISSING);
    check_equal(memcmp(&out, &zero, sizeof(out)), 0);
  }
}

#undef TOKEN_KEY
#undef TOKEN_SINT
#undef TOKEN_ARRAY_END
#undef TOKEN_ARRAY_BEGIN
#undef TOKEN_MAP_END
#undef TOKEN_MAP_BEGIN
#undef CBIND_DATA_PREFIX_SIZE
