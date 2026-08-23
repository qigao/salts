#include <cbind/cbind.h>
#include "recording.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>

#define CBIND_DATA_PREFIX_SIZE \
    (offsetof(cmeta_data_desc, shape) + sizeof(((cmeta_data_desc *)0)->shape))

#define CBIND_TOKEN_MAP_BEGIN { .kind = CSERDE_MAP_BEGIN }
#define CBIND_TOKEN_MAP_END { .kind = CSERDE_MAP_END }
#define CBIND_TOKEN_ARRAY_BEGIN { .kind = CSERDE_ARRAY_BEGIN }
#define CBIND_TOKEN_SINT(v) { .kind = CSERDE_SINT, .value.sint = (v) }
#define CBIND_TOKEN_UINT(v) { .kind = CSERDE_UINT, .value.uint = (v) }
#define CBIND_TOKEN_FLOAT(v) { .kind = CSERDE_FLOAT, .value.floating = (v) }
#define CBIND_TOKEN_BOOL(v) { .kind = CSERDE_BOOL, .value.boolean = (v) }
#define CBIND_TOKEN_KEY(text) \
    { .kind = CSERDE_STRING, \
      .value.slice = { (const unsigned char *)(text), sizeof(text) - 1u, \
                       CSERDE_VIEW_STABLE } }

Struct(cbind_runtime_empty,
    (int, marker)
);

Struct(cbind_runtime_flat,
    (int, id),
    (long, score)
);

Struct(cbind_runtime_inner,
    (int, count),
    (double, ratio)
);

Struct(cbind_runtime_record,
    (int, id),
    (long, score),
    (cbind_runtime_inner, inner),
    (int, untouched)
);

Struct(cbind_runtime_siblings,
    (cbind_runtime_inner, left),
    (cbind_runtime_inner, right)
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

DEFINE_OBJECT_TYPE(cbind_runtime_empty_type, cbind_runtime_empty,
                   "test.cbind.runtime.empty");
DEFINE_OBJECT_TYPE(cbind_runtime_flat_type, cbind_runtime_flat,
                   "test.cbind.runtime.flat");
DEFINE_OBJECT_TYPE(cbind_runtime_inner_type, cbind_runtime_inner,
                   "test.cbind.runtime.inner");
DEFINE_OBJECT_TYPE(cbind_runtime_record_type, cbind_runtime_record,
                   "test.cbind.runtime.record");
DEFINE_OBJECT_TYPE(cbind_runtime_siblings_type, cbind_runtime_siblings,
                   "test.cbind.runtime.siblings");

static const cmeta_data_struct_shape cbind_runtime_empty_shape = {
    .layout = StructMeta(cbind_runtime_empty),
    .fields = NULL,
    .field_count = 0u
};

static const cmeta_data_desc cbind_runtime_empty_data = {
    .struct_size = CBIND_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.runtime.empty.data",
    .display_name = "runtime-empty",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cbind_runtime_empty_type,
    .shape = &cbind_runtime_empty_shape
};

static const cmeta_data_field_desc cbind_runtime_flat_fields[] = {
    {"test.cbind.runtime.flat.id", "id", offsetof(cbind_runtime_flat, id),
     &cmeta_data_int},
    {"test.cbind.runtime.flat.score", "score", offsetof(cbind_runtime_flat, score),
     &cmeta_data_long}
};

static const cmeta_data_struct_shape cbind_runtime_flat_shape = {
    .layout = StructMeta(cbind_runtime_flat),
    .fields = cbind_runtime_flat_fields,
    .field_count = 2u
};

static const cmeta_data_desc cbind_runtime_flat_data = {
    .struct_size = CBIND_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.runtime.flat.data",
    .display_name = "runtime-flat",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cbind_runtime_flat_type,
    .shape = &cbind_runtime_flat_shape
};

static const cmeta_data_field_desc cbind_runtime_inner_fields[] = {
    {"test.cbind.runtime.inner.count", "count", offsetof(cbind_runtime_inner, count),
     &cmeta_data_int},
    {"test.cbind.runtime.inner.ratio", "ratio", offsetof(cbind_runtime_inner, ratio),
     &cmeta_data_double}
};

static const cmeta_data_struct_shape cbind_runtime_inner_shape = {
    .layout = StructMeta(cbind_runtime_inner),
    .fields = cbind_runtime_inner_fields,
    .field_count = 2u
};

static const cmeta_data_desc cbind_runtime_inner_data = {
    .struct_size = CBIND_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.runtime.inner.data",
    .display_name = "runtime-inner",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cbind_runtime_inner_type,
    .shape = &cbind_runtime_inner_shape
};

static const cmeta_data_field_desc cbind_runtime_record_fields[] = {
    {"test.cbind.runtime.record.id", "id", offsetof(cbind_runtime_record, id),
     &cmeta_data_int},
    {"test.cbind.runtime.record.score", "score", offsetof(cbind_runtime_record, score),
     &cmeta_data_long},
    {"test.cbind.runtime.record.inner", "inner", offsetof(cbind_runtime_record, inner),
     &cbind_runtime_inner_data}
};

static const cmeta_data_struct_shape cbind_runtime_record_shape = {
    .layout = StructMeta(cbind_runtime_record),
    .fields = cbind_runtime_record_fields,
    .field_count = 3u
};

static const cmeta_data_desc cbind_runtime_record_data = {
    .struct_size = CBIND_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.runtime.record.data",
    .display_name = "runtime-record",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cbind_runtime_record_type,
    .shape = &cbind_runtime_record_shape
};

static const cmeta_data_field_desc cbind_runtime_siblings_fields[] = {
    {"test.cbind.runtime.siblings.left", "left", offsetof(cbind_runtime_siblings, left),
     &cbind_runtime_inner_data},
    {"test.cbind.runtime.siblings.right", "right", offsetof(cbind_runtime_siblings, right),
     &cbind_runtime_inner_data}
};

static const cmeta_data_struct_shape cbind_runtime_siblings_shape = {
    .layout = StructMeta(cbind_runtime_siblings),
    .fields = cbind_runtime_siblings_fields,
    .field_count = 2u
};

static const cmeta_data_desc cbind_runtime_siblings_data = {
    .struct_size = CBIND_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.runtime.siblings.data",
    .display_name = "runtime-siblings",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cbind_runtime_siblings_type,
    .shape = &cbind_runtime_siblings_shape
};

#undef DEFINE_OBJECT_TYPE

static cbind_status decode_tokens(const cmeta_data_desc *shape,
                                  const cserde_token *tokens,
                                  size_t token_count,
                                  void *out,
                                  size_t max_depth,
                                  void *scratch,
                                  size_t scratch_size,
                                  cbind_error *error,
                                  size_t *source_index) {
    cserde_recording_reader_context source = {tokens, token_count, 0u};
    cserde_reader reader = {0};
    cbind_context context = CBIND_CONTEXT_INIT(scratch, scratch_size, max_depth);
    cbind_status status;

    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops, &source),
                CSERDE_OK);
    status = cbind_decode(&context, shape, &reader, out, error);
    if (source_index != NULL)
        *source_index = source.index;
    return status;
}

spec("CBind strict struct runtime decode") {
  it("decodes an empty semantic struct without touching nonsemantic storage") {
    const cserde_token tokens[] = {CBIND_TOKEN_MAP_BEGIN, CBIND_TOKEN_MAP_END};
    cbind_runtime_empty out = {.marker = 77};
    cbind_error error = CBIND_ERROR_INIT;

    check_equal(decode_tokens(&cbind_runtime_empty_data, tokens,
                              sizeof(tokens) / sizeof(tokens[0]), &out,
                              1u, NULL, 0u, &error, NULL),
                CBIND_OK);
    check_equal(out.marker, 77);
  }

  it("decodes flat struct fields in descriptor order") {
    const cserde_token tokens[] = {
        CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_KEY("id"), CBIND_TOKEN_SINT(7),
        CBIND_TOKEN_KEY("score"), CBIND_TOKEN_SINT(19),
        CBIND_TOKEN_MAP_END
    };
    cbind_runtime_flat out = {0};
    unsigned char scratch[1] = {0};
    cbind_error error = CBIND_ERROR_INIT;

    check_equal(decode_tokens(&cbind_runtime_flat_data, tokens,
                              sizeof(tokens) / sizeof(tokens[0]), &out,
                              1u, scratch, sizeof(scratch), &error, NULL),
                CBIND_OK);
    check_equal(out.id, 7);
    check_equal(out.score, 19L);
  }

  it("decodes flat struct fields in reverse input order") {
    const cserde_token tokens[] = {
        CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_KEY("score"), CBIND_TOKEN_SINT(19),
        CBIND_TOKEN_KEY("id"), CBIND_TOKEN_SINT(7),
        CBIND_TOKEN_MAP_END
    };
    cbind_runtime_flat out = {0};
    unsigned char scratch[1] = {0};
    cbind_error error = CBIND_ERROR_INIT;

    check_equal(decode_tokens(&cbind_runtime_flat_data, tokens,
                              sizeof(tokens) / sizeof(tokens[0]), &out,
                              1u, scratch, sizeof(scratch), &error, NULL),
                CBIND_OK);
    check_equal(out.id, 7);
    check_equal(out.score, 19L);
  }

  it("decodes nested structs and leaves nonsemantic field unchanged") {
    const cserde_token tokens[] = {
        CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_KEY("id"), CBIND_TOKEN_SINT(3),
        CBIND_TOKEN_KEY("score"), CBIND_TOKEN_SINT(8),
        CBIND_TOKEN_KEY("inner"), CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_KEY("count"), CBIND_TOKEN_SINT(5),
        CBIND_TOKEN_KEY("ratio"), CBIND_TOKEN_FLOAT(1.5),
        CBIND_TOKEN_MAP_END,
        CBIND_TOKEN_MAP_END
    };
    cbind_runtime_record out = {0};
    unsigned char scratch[2] = {0};
    cbind_error error = CBIND_ERROR_INIT;

    out.untouched = 123;
    check_equal(decode_tokens(&cbind_runtime_record_data, tokens,
                              sizeof(tokens) / sizeof(tokens[0]), &out,
                              2u, scratch, sizeof(scratch), &error, NULL),
                CBIND_OK);
    check_equal(out.id, 3);
    check_equal(out.score, 8L);
    check_equal(out.inner.count, 5);
    check_true(out.inner.ratio == 1.5);
    check_equal(out.untouched, 123);
  }

  it("accepts a transient non-NUL key slice") {
    static const unsigned char id_key[] = {'i', 'd'};
    cserde_token tokens[] = {
        CBIND_TOKEN_MAP_BEGIN,
        {.kind = CSERDE_STRING,
         .value.slice = {id_key, sizeof(id_key), CSERDE_VIEW_TRANSIENT}},
        CBIND_TOKEN_SINT(7),
        CBIND_TOKEN_KEY("score"), CBIND_TOKEN_SINT(9),
        CBIND_TOKEN_MAP_END
    };
    cbind_runtime_flat out = {0};
    unsigned char scratch[1] = {0};
    cbind_error error = CBIND_ERROR_INIT;

    check_equal(decode_tokens(&cbind_runtime_flat_data, tokens,
                              sizeof(tokens) / sizeof(tokens[0]), &out,
                              1u, scratch, sizeof(scratch), &error, NULL),
                CBIND_OK);
    check_equal(out.id, 7);
    check_equal(out.score, 9L);
  }

  it("rejects case-mismatched and unknown keys before consuming values") {
    const cserde_token case_tokens[] = {
        CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_KEY("ID"), CBIND_TOKEN_SINT(7),
        CBIND_TOKEN_MAP_END
    };
    const cserde_token unknown_tokens[] = {
        CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_KEY("missing"), CBIND_TOKEN_SINT(7),
        CBIND_TOKEN_MAP_END
    };
    cbind_runtime_flat out = {0};
    unsigned char scratch[1] = {0};
    cbind_error error = CBIND_ERROR_INIT;
    size_t index = SIZE_MAX;

    check_equal(decode_tokens(&cbind_runtime_flat_data, case_tokens,
                              sizeof(case_tokens) / sizeof(case_tokens[0]), &out,
                              1u, scratch, sizeof(scratch), &error, &index),
                CBIND_UNKNOWN_FIELD);
    check_equal(index, (size_t)2u);
    check_null(error.field);
    check_equal(error.depth, (size_t)1u);

    error = (cbind_error)CBIND_ERROR_INIT;
    index = SIZE_MAX;
    check_equal(decode_tokens(&cbind_runtime_flat_data, unknown_tokens,
                              sizeof(unknown_tokens) / sizeof(unknown_tokens[0]), &out,
                              1u, scratch, sizeof(scratch), &error, &index),
                CBIND_UNKNOWN_FIELD);
    check_equal(index, (size_t)2u);
  }

  it("rejects duplicate fields before consuming the duplicate value") {
    const cserde_token tokens[] = {
        CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_KEY("id"), CBIND_TOKEN_SINT(1),
        CBIND_TOKEN_KEY("id"), CBIND_TOKEN_SINT(2),
        CBIND_TOKEN_KEY("score"), CBIND_TOKEN_SINT(3),
        CBIND_TOKEN_MAP_END
    };
    cbind_runtime_flat out = {0};
    unsigned char scratch[1] = {0};
    cbind_error error = CBIND_ERROR_INIT;
    size_t index = SIZE_MAX;

    check_equal(decode_tokens(&cbind_runtime_flat_data, tokens,
                              sizeof(tokens) / sizeof(tokens[0]), &out,
                              1u, scratch, sizeof(scratch), &error, &index),
                CBIND_DUPLICATE_FIELD);
    check_equal(index, (size_t)4u);
    check_true(error.field == &cbind_runtime_flat_fields[0]);
    check_equal(error.depth, (size_t)1u);
  }

  it("reports the first missing field in semantic descriptor order") {
    const cserde_token tokens[] = {
        CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_KEY("id"), CBIND_TOKEN_SINT(1),
        CBIND_TOKEN_MAP_END
    };
    cbind_runtime_flat out = {0};
    unsigned char scratch[1] = {0};
    cbind_error error = CBIND_ERROR_INIT;

    check_equal(decode_tokens(&cbind_runtime_flat_data, tokens,
                              sizeof(tokens) / sizeof(tokens[0]), &out,
                              1u, scratch, sizeof(scratch), &error, NULL),
                CBIND_MISSING_FIELD);
    check_true(error.field == &cbind_runtime_flat_fields[1]);
    check_equal(error.depth, (size_t)1u);
  }

  it("rejects non-string keys and wrong root tokens at the actual position") {
    const cserde_token key_tokens[] = {
        CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_UINT(1), CBIND_TOKEN_SINT(7),
        CBIND_TOKEN_MAP_END
    };
    const cserde_token root_tokens[] = {CBIND_TOKEN_ARRAY_BEGIN};
    cbind_runtime_flat out = {0};
    unsigned char scratch[1] = {0};
    cbind_error error = CBIND_ERROR_INIT;
    size_t index = SIZE_MAX;

    check_equal(decode_tokens(&cbind_runtime_flat_data, key_tokens,
                              sizeof(key_tokens) / sizeof(key_tokens[0]), &out,
                              1u, scratch, sizeof(scratch), &error, &index),
                CBIND_TOKEN_MISMATCH);
    check_equal(index, (size_t)2u);

    error = (cbind_error)CBIND_ERROR_INIT;
    index = SIZE_MAX;
    check_equal(decode_tokens(&cbind_runtime_flat_data, root_tokens,
                              sizeof(root_tokens) / sizeof(root_tokens[0]), &out,
                              1u, scratch, sizeof(scratch), &error, &index),
                CBIND_TOKEN_MISMATCH);
    check_equal(index, (size_t)1u);
  }

  it("attributes a known field value mismatch to the child semantic shape") {
    const cserde_token tokens[] = {
        CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_KEY("id"), CBIND_TOKEN_BOOL(true),
        CBIND_TOKEN_KEY("score"), CBIND_TOKEN_SINT(9),
        CBIND_TOKEN_MAP_END
    };
    cbind_runtime_flat out = {0};
    unsigned char scratch[1] = {0};
    cbind_error error = CBIND_ERROR_INIT;
    size_t index = SIZE_MAX;

    check_equal(decode_tokens(&cbind_runtime_flat_data, tokens,
                              sizeof(tokens) / sizeof(tokens[0]), &out,
                              1u, scratch, sizeof(scratch), &error, &index),
                CBIND_TOKEN_MISMATCH);
    check_equal(index, (size_t)3u);
    check_true(error.shape == &cmeta_data_int);
    check_true(error.field == &cbind_runtime_flat_fields[0]);
    check_equal(error.depth, (size_t)1u);
  }

  it("rewinds sibling scratch frames after each nested value") {
    const cserde_token tokens[] = {
        CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_KEY("left"), CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_KEY("count"), CBIND_TOKEN_SINT(1),
        CBIND_TOKEN_KEY("ratio"), CBIND_TOKEN_FLOAT(1.25),
        CBIND_TOKEN_MAP_END,
        CBIND_TOKEN_KEY("right"), CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_KEY("count"), CBIND_TOKEN_SINT(2),
        CBIND_TOKEN_KEY("ratio"), CBIND_TOKEN_FLOAT(2.5),
        CBIND_TOKEN_MAP_END,
        CBIND_TOKEN_MAP_END
    };
    cbind_runtime_siblings out = {0};
    unsigned char scratch[2] = {0};
    cbind_error error = CBIND_ERROR_INIT;

    check_equal(decode_tokens(&cbind_runtime_siblings_data, tokens,
                              sizeof(tokens) / sizeof(tokens[0]), &out,
                              2u, scratch, sizeof(scratch), &error, NULL),
                CBIND_OK);
    check_equal(out.left.count, 1);
    check_true(out.left.ratio == 1.25);
    check_equal(out.right.count, 2);
    check_true(out.right.ratio == 2.5);
  }
}

#undef CBIND_TOKEN_KEY
#undef CBIND_TOKEN_BOOL
#undef CBIND_TOKEN_FLOAT
#undef CBIND_TOKEN_UINT
#undef CBIND_TOKEN_SINT
#undef CBIND_TOKEN_ARRAY_BEGIN
#undef CBIND_TOKEN_MAP_END
#undef CBIND_TOKEN_MAP_BEGIN
#undef CBIND_DATA_PREFIX_SIZE
