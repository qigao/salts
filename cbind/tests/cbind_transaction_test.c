#include <cbind/cbind.h>
#include "recording.h"
#include "tinytest.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#define CBIND_DATA_PREFIX_SIZE \
    (offsetof(cmeta_data_desc, shape) + sizeof(((cmeta_data_desc *)0)->shape))

#define CBIND_TOKEN_MAP_BEGIN { .kind = CSERDE_MAP_BEGIN }
#define CBIND_TOKEN_MAP_END { .kind = CSERDE_MAP_END }
#define CBIND_TOKEN_SINT(v) { .kind = CSERDE_SINT, .value.sint = (v) }
#define CBIND_TOKEN_BOOL(v) { .kind = CSERDE_BOOL, .value.boolean = (v) }
#define CBIND_TOKEN_KEY(text) \
    { .kind = CSERDE_STRING, \
      .value.slice = { (const unsigned char *)(text), sizeof(text) - 1u, \
                       CSERDE_VIEW_STABLE } }

Struct(cbind_tx_inner,
    (int, count),
    (double, ratio)
);

Struct(cbind_tx_record,
    (int, id),
    (cbind_tx_inner, inner),
    (int, untouched)
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

DEFINE_OBJECT_TYPE(cbind_tx_inner_type, cbind_tx_inner,
                   "test.cbind.tx.inner");
DEFINE_OBJECT_TYPE(cbind_tx_record_type, cbind_tx_record,
                   "test.cbind.tx.record");

static const cmeta_data_field_desc cbind_tx_inner_fields[] = {
    {"test.cbind.tx.inner.count", "count", offsetof(cbind_tx_inner, count),
     &cmeta_data_int},
    {"test.cbind.tx.inner.ratio", "ratio", offsetof(cbind_tx_inner, ratio),
     &cmeta_data_double}
};

static const cmeta_data_struct_shape cbind_tx_inner_shape = {
    .layout = StructMeta(cbind_tx_inner),
    .fields = cbind_tx_inner_fields,
    .field_count = 2u
};

static const cmeta_data_desc cbind_tx_inner_data = {
    .struct_size = CBIND_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.tx.inner.data",
    .display_name = "tx-inner",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cbind_tx_inner_type,
    .shape = &cbind_tx_inner_shape
};

static const cmeta_data_field_desc cbind_tx_record_fields[] = {
    {"test.cbind.tx.record.id", "id", offsetof(cbind_tx_record, id),
     &cmeta_data_int},
    {"test.cbind.tx.record.inner", "inner", offsetof(cbind_tx_record, inner),
     &cbind_tx_inner_data}
};

static const cmeta_data_struct_shape cbind_tx_record_shape = {
    .layout = StructMeta(cbind_tx_record),
    .fields = cbind_tx_record_fields,
    .field_count = 2u
};

static const cmeta_data_desc cbind_tx_record_data = {
    .struct_size = CBIND_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.tx.record.data",
    .display_name = "tx-record",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cbind_tx_record_type,
    .shape = &cbind_tx_record_shape
};

#undef DEFINE_OBJECT_TYPE

typedef struct failing_reader_context {
    const cserde_token *prefix;
    size_t prefix_count;
    size_t index;
    size_t calls;
    cserde_status failure;
} failing_reader_context;

static cserde_status failing_reader_next(void *context, cserde_token *out) {
    failing_reader_context *state = (failing_reader_context *)context;

    ++state->calls;
    if (state->index < state->prefix_count) {
        *out = state->prefix[state->index++];
        return CSERDE_OK;
    }
    return state->failure;
}

static const cserde_reader_ops failing_reader_ops = {
    offsetof(cserde_reader_ops, next) + sizeof(cserde_reader_next_fn),
    CSERDE_READER_OPS_ABI_VERSION,
    failing_reader_next
};

static cbind_status decode_recording(const cmeta_data_desc *shape,
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

static cbind_status decode_failing(const cmeta_data_desc *shape,
                                   failing_reader_context *source,
                                   void *out,
                                   size_t max_depth,
                                   void *scratch,
                                   size_t scratch_size,
                                   cbind_error *error) {
    cserde_reader reader = {0};
    cbind_context context = CBIND_CONTEXT_INIT(scratch, scratch_size, max_depth);

    check_equal(cserde_reader_init(&reader, &failing_reader_ops, source),
                CSERDE_OK);
    return cbind_decode(&context, shape, &reader, out, error);
}

spec("CBind canonical storage identity proof") {
  it("treats canonical int identity with forged storage size as invalid shape") {
    cmeta_type_desc bad_type = cmeta_type_int;
    cmeta_data_desc bad_data = cmeta_data_int;
    const cserde_token token = CBIND_TOKEN_SINT(1);
    cserde_recording_reader_context source = {&token, 1u, 0u};
    cserde_reader reader = {0};
    cbind_context context = CBIND_CONTEXT_INIT(NULL, 0u, 0u);
    cbind_error error = CBIND_ERROR_INIT;
    int out = 0;

    bad_type.size += 1u;
    bad_data.storage_type = &bad_type;
    check_true(cmeta_data_desc_valid(&bad_data));
    check_true(cmeta_type_equal(&bad_type, &cmeta_type_int));
    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops, &source),
                CSERDE_OK);
    check_equal(cbind_decode(&context, &bad_data, &reader, &out, &error),
                CBIND_INVALID_SHAPE);
    check_equal(source.index, (size_t)0u);
  }

  it("treats canonical int identity with forged alignment as invalid shape") {
    cmeta_type_desc bad_type = cmeta_type_int;
    cmeta_data_desc bad_data = cmeta_data_int;
    const cserde_token token = CBIND_TOKEN_SINT(1);
    cserde_recording_reader_context source = {&token, 1u, 0u};
    cserde_reader reader = {0};
    cbind_context context = CBIND_CONTEXT_INIT(NULL, 0u, 0u);
    cbind_error error = CBIND_ERROR_INIT;
    int out = 0;

    bad_type.align += 1u;
    bad_data.storage_type = &bad_type;
    check_true(cmeta_data_desc_valid(&bad_data));
    check_true(cmeta_type_equal(&bad_type, &cmeta_type_int));
    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops, &source),
                CSERDE_OK);
    check_equal(cbind_decode(&context, &bad_data, &reader, &out, &error),
                CBIND_INVALID_SHAPE);
    check_equal(source.index, (size_t)0u);
  }
}

spec("CBind source status mapping") {
  it("maps scalar DONE to unexpected end at depth zero") {
    failing_reader_context source = {NULL, 0u, 0u, 0u, CSERDE_DONE};
    cbind_error error = CBIND_ERROR_INIT;
    int out = 0;

    check_equal(decode_failing(&cmeta_data_int, &source, &out,
                               0u, NULL, 0u, &error),
                CBIND_UNEXPECTED_END);
    check_equal(error.source_status, CSERDE_DONE);
    check_true(error.shape == &cmeta_data_int);
    check_null(error.field);
    check_equal(error.depth, (size_t)0u);
    check_equal(source.calls, (size_t)1u);
  }

  it("maps struct DONE to unexpected end at depth one") {
    failing_reader_context source = {NULL, 0u, 0u, 0u, CSERDE_DONE};
    cbind_tx_record out = {0};
    unsigned char scratch[2] = {0};
    cbind_error error = CBIND_ERROR_INIT;

    out.untouched = 91;
    check_equal(decode_failing(&cbind_tx_record_data, &source, &out,
                               2u, scratch, sizeof(scratch), &error),
                CBIND_UNEXPECTED_END);
    check_equal(error.source_status, CSERDE_DONE);
    check_true(error.shape == &cbind_tx_record_data);
    check_null(error.field);
    check_equal(error.depth, (size_t)1u);
    check_equal(source.calls, (size_t)1u);
    check_equal(out.untouched, 91);
  }

  it("maps provider SOURCE_ERROR and VALUE_OUT_OF_RANGE without relabeling") {
    failing_reader_context source_error = {NULL, 0u, 0u, 0u,
                                           CSERDE_SOURCE_ERROR};
    failing_reader_context range_error = {NULL, 0u, 0u, 0u,
                                          CSERDE_VALUE_OUT_OF_RANGE};
    cbind_error error = CBIND_ERROR_INIT;
    int out = 0;

    check_equal(decode_failing(&cmeta_data_int, &source_error, &out,
                               0u, NULL, 0u, &error),
                CBIND_SOURCE_ERROR);
    check_equal(error.source_status, CSERDE_SOURCE_ERROR);
    check_equal(source_error.calls, (size_t)1u);

    error = (cbind_error)CBIND_ERROR_INIT;
    check_equal(decode_failing(&cmeta_data_int, &range_error, &out,
                               0u, NULL, 0u, &error),
                CBIND_SOURCE_ERROR);
    check_equal(error.source_status, CSERDE_VALUE_OUT_OF_RANGE);
    check_equal(range_error.calls, (size_t)1u);
  }

  it("preserves CSerde callback normalization and makes no extra provider call") {
    failing_reader_context source = {NULL, 0u, 0u, 0u,
                                     CSERDE_INVALID_ARGUMENT};
    cbind_error error = CBIND_ERROR_INIT;
    int out = 0;

    check_equal(decode_failing(&cmeta_data_int, &source, &out,
                               0u, NULL, 0u, &error),
                CBIND_SOURCE_ERROR);
    check_equal(error.source_status, CSERDE_CALLBACK_ERROR);
    check_equal(source.calls, (size_t)1u);
  }

  it("maps mid-struct DONE at the current struct boundary") {
    const cserde_token prefix[] = {
        CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_KEY("id"), CBIND_TOKEN_SINT(5)
    };
    failing_reader_context source = {
        prefix, sizeof(prefix) / sizeof(prefix[0]), 0u, 0u, CSERDE_DONE
    };
    cbind_tx_record out = {0};
    unsigned char scratch[2] = {0};
    cbind_error error = CBIND_ERROR_INIT;

    out.untouched = 37;
    check_equal(decode_failing(&cbind_tx_record_data, &source, &out,
                               2u, scratch, sizeof(scratch), &error),
                CBIND_UNEXPECTED_END);
    check_equal(error.source_status, CSERDE_DONE);
    check_true(error.shape == &cbind_tx_record_data);
    check_null(error.field);
    check_equal(error.depth, (size_t)1u);
    check_equal(source.calls, (size_t)4u);
    check_equal(out.id, 0);
    check_equal(out.untouched, 37);
  }
}

spec("CBind transactional rollback and diagnostics") {
  it("rolls back earlier fields after a later nested token mismatch") {
    const cserde_token tokens[] = {
        CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_KEY("id"), CBIND_TOKEN_SINT(11),
        CBIND_TOKEN_KEY("inner"), CBIND_TOKEN_BOOL(true),
        CBIND_TOKEN_MAP_END
    };
    cbind_tx_record out = {0};
    unsigned char scratch[2] = {0};
    cbind_error error = CBIND_ERROR_INIT;

    out.untouched = 1234;
    check_equal(decode_recording(&cbind_tx_record_data, tokens,
                                 sizeof(tokens) / sizeof(tokens[0]), &out,
                                 2u, scratch, sizeof(scratch), &error, NULL),
                CBIND_TOKEN_MISMATCH);
    check_equal(out.id, 0);
    check_equal(out.inner.count, 0);
    check_true(out.inner.ratio == 0.0);
    check_equal(out.untouched, 1234);
    check_true(error.shape == &cbind_tx_inner_data);
    check_true(error.field == &cbind_tx_record_fields[1]);
    check_equal(error.depth, (size_t)2u);
  }

  it("rolls back the complete root after a nested missing field") {
    const cserde_token tokens[] = {
        CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_KEY("id"), CBIND_TOKEN_SINT(7),
        CBIND_TOKEN_KEY("inner"), CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_KEY("count"), CBIND_TOKEN_SINT(3),
        CBIND_TOKEN_MAP_END,
        CBIND_TOKEN_MAP_END
    };
    cbind_tx_record out = {0};
    unsigned char scratch[2] = {0};
    cbind_error error = CBIND_ERROR_INIT;

    out.untouched = 88;
    check_equal(decode_recording(&cbind_tx_record_data, tokens,
                                 sizeof(tokens) / sizeof(tokens[0]), &out,
                                 2u, scratch, sizeof(scratch), &error, NULL),
                CBIND_MISSING_FIELD);
    check_equal(out.id, 0);
    check_equal(out.inner.count, 0);
    check_true(out.inner.ratio == 0.0);
    check_equal(out.untouched, 88);
    check_true(error.shape == &cbind_tx_inner_data);
    check_true(error.field == &cbind_tx_inner_fields[1]);
    check_equal(error.depth, (size_t)2u);
  }

  it("attributes root scalar and known field mismatches deterministically") {
    const cserde_token scalar_token = CBIND_TOKEN_BOOL(true);
    const cserde_token field_tokens[] = {
        CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_KEY("id"), CBIND_TOKEN_BOOL(true),
        CBIND_TOKEN_KEY("inner"), CBIND_TOKEN_MAP_BEGIN,
        CBIND_TOKEN_KEY("count"), CBIND_TOKEN_SINT(1),
        CBIND_TOKEN_KEY("ratio"), {.kind = CSERDE_FLOAT, .value.floating = 1.0},
        CBIND_TOKEN_MAP_END,
        CBIND_TOKEN_MAP_END
    };
    int scalar_out = 0;
    cbind_tx_record record_out = {0};
    unsigned char scratch[2] = {0};
    cbind_error error = CBIND_ERROR_INIT;

    check_equal(decode_recording(&cmeta_data_int, &scalar_token, 1u,
                                 &scalar_out, 0u, NULL, 0u, &error, NULL),
                CBIND_TOKEN_MISMATCH);
    check_true(error.shape == &cmeta_data_int);
    check_null(error.field);
    check_equal(error.depth, (size_t)0u);

    error = (cbind_error)CBIND_ERROR_INIT;
    check_equal(decode_recording(&cbind_tx_record_data, field_tokens,
                                 sizeof(field_tokens) / sizeof(field_tokens[0]),
                                 &record_out, 2u, scratch, sizeof(scratch),
                                 &error, NULL),
                CBIND_TOKEN_MISMATCH);
    check_true(error.shape == &cmeta_data_int);
    check_true(error.field == &cbind_tx_record_fields[0]);
    check_equal(error.depth, (size_t)1u);
  }
}

#undef CBIND_TOKEN_KEY
#undef CBIND_TOKEN_BOOL
#undef CBIND_TOKEN_SINT
#undef CBIND_TOKEN_MAP_END
#undef CBIND_TOKEN_MAP_BEGIN
#undef CBIND_DATA_PREFIX_SIZE
