#include <cbind/cbind.h>
#include "salts_cmeta_data.h"
#include "salts_str.h"
#include "salts_vstr.h"
#include "recording.h"
#include "tinytest.h"

#include <stddef.h>
#include <string.h>

#define TOKEN_MAP_BEGIN { .kind = CSERDE_MAP_BEGIN }
#define TOKEN_MAP_END { .kind = CSERDE_MAP_END }
#define TOKEN_SINT(value_) { .kind = CSERDE_SINT, .value.sint = (value_) }
#define TOKEN_SLICE(kind_, data_, size_, lifetime_)                       \
    { .kind = (kind_),                                                   \
      .value.slice = {(const unsigned char *)(data_), (size_),           \
                      (lifetime_)} }
#define TOKEN_KEY(text_)                                                  \
    TOKEN_SLICE(CSERDE_STRING, (text_), sizeof(text_) - 1u,               \
                CSERDE_VIEW_STABLE)

static const cmeta_data_buffer_shape owned_shape = {
    .ownership = CMETA_DATA_BUFFER_OWNED
};
static const cmeta_data_buffer_shape borrowed_shape = {
    .ownership = CMETA_DATA_BUFFER_BORROWED
};

static const cmeta_data_desc owned_string = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.owned-string",
    .display_name = "owned string",
    .kind = CMETA_DATA_STRING,
    .storage_type = &salts_tstr_cmeta_type,
    .shape = &owned_shape,
    .buffer_ops = &salts_tstr_cmeta_buffer_ops
};
static const cmeta_data_desc owned_bytes = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.owned-bytes",
    .display_name = "owned bytes",
    .kind = CMETA_DATA_BYTES,
    .storage_type = &salts_tstr_cmeta_type,
    .shape = &owned_shape,
    .buffer_ops = &salts_tstr_cmeta_buffer_ops
};
static const cmeta_data_desc borrowed_string = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.borrowed-string",
    .display_name = "borrowed string",
    .kind = CMETA_DATA_STRING,
    .storage_type = &salts_vstr_cmeta_type,
    .shape = &borrowed_shape,
    .buffer_ops = &salts_vstr_cmeta_buffer_ops
};

Struct(cbind_buffer_record,
    (int, id),
    (tstr, payload),
    (vstr, alias)
);

static const cmeta_type_identity record_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.cbind.buffer-record");
static const cmeta_type_desc record_type = {
    .name = "cbind_buffer_record",
    .size = sizeof(cbind_buffer_record),
    .align = _Alignof(cbind_buffer_record),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = &record_identity
};
static const cmeta_data_field_desc record_fields[] = {
    {"test.cbind.buffer-record.id", "id",
     offsetof(cbind_buffer_record, id), &cmeta_data_int},
    {"test.cbind.buffer-record.payload", "payload",
     offsetof(cbind_buffer_record, payload), &owned_bytes},
    {"test.cbind.buffer-record.alias", "alias",
     offsetof(cbind_buffer_record, alias), &borrowed_string}
};
static const cmeta_data_struct_shape record_shape = {
    .layout = StructMeta(cbind_buffer_record),
    .fields = record_fields,
    .field_count = sizeof(record_fields) / sizeof(record_fields[0])
};
static const cmeta_data_desc record_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.buffer-record.data",
    .display_name = "buffer record",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &record_type,
    .shape = &record_shape,
    .buffer_ops = NULL
};

static cbind_status decode_recording(const cmeta_data_desc *shape,
                                     const cserde_token *tokens,
                                     size_t token_count,
                                     void *out,
                                     size_t max_depth,
                                     size_t max_buffer_bytes,
                                     size_t context_size,
                                     cbind_error *error,
                                     size_t *source_index) {
    unsigned char scratch[1] = {0};
    cserde_recording_reader_context source = {tokens, token_count, 0u};
    cserde_reader reader = {0};
    cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
        scratch, sizeof(scratch), max_depth, 0u, max_buffer_bytes);
    cbind_status status;

    context.struct_size = context_size;
    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops,
                                   &source), CSERDE_OK);
    status = cbind_decode(&context, shape, &reader, out, error);
    if (source_index != NULL)
        *source_index = source.index;
    return status;
}

static cbind_status decode_full(const cmeta_data_desc *shape,
                                const cserde_token *tokens,
                                size_t token_count,
                                void *out,
                                size_t max_depth,
                                size_t max_buffer_bytes,
                                cbind_error *error,
                                size_t *source_index) {
    return decode_recording(shape, tokens, token_count, out, max_depth,
                            max_buffer_bytes, sizeof(cbind_context), error,
                            source_index);
}

static bool failing_is_zero(const void *object) {
    return object != NULL && *(const tstr *)object == NULL;
}

static cmeta_status failing_assign(void *object,
                                   const unsigned char *data,
                                   size_t size,
                                   size_t max_bytes) {
    (void)object;
    (void)data;
    (void)size;
    (void)max_bytes;
    return CMETA_OUT_OF_MEMORY;
}

static void failing_restore(void *object) {
    if (object != NULL)
        *(tstr *)object = NULL;
}

static const cmeta_data_buffer_ops failing_ops = {
    sizeof(cmeta_data_buffer_ops), CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    &salts_tstr_cmeta_type, CMETA_DATA_BUFFER_OWNED,
    failing_is_zero, failing_assign, failing_restore
};

spec("CBind buffer preflight") {
  it("requires adapter metadata and the extended context before input") {
    const cserde_token token = TOKEN_SLICE(
        CSERDE_STRING, "x", 1u, CSERDE_VIEW_STABLE);
    cmeta_data_desc legacy = owned_string;
    tstr out = NULL;
    size_t source_index = SIZE_MAX;

    legacy.struct_size = offsetof(cmeta_data_desc, shape) +
                         sizeof(legacy.shape);
    check_equal(decode_full(&legacy, &token, 1u, &out, 0u, 1u, NULL,
                            &source_index), CBIND_UNSUPPORTED);
    check_equal(source_index, (size_t)0u);

    check_equal(decode_recording(
                    &owned_string, &token, 1u, &out, 0u, 1u,
                    offsetof(cbind_context, max_container_items) +
                        sizeof(((cbind_context *)0)->max_container_items),
                    NULL, &source_index),
                CBIND_INVALID_CONTEXT);
    check_equal(source_index, (size_t)0u);
  }

  it("rejects malformed and custom adapters before input") {
    const cserde_token token = TOKEN_SLICE(
        CSERDE_STRING, "x", 1u, CSERDE_VIEW_STABLE);
    cmeta_data_buffer_ops ops = salts_tstr_cmeta_buffer_ops;
    cmeta_data_buffer_shape shape = owned_shape;
    cmeta_data_desc data = owned_string;
    tstr out = NULL;
    size_t source_index = SIZE_MAX;

    ops.abi_version += 1u;
    data.buffer_ops = &ops;
    check_equal(decode_full(&data, &token, 1u, &out, 0u, 1u, NULL,
                            &source_index), CBIND_INVALID_SHAPE);
    check_equal(source_index, (size_t)0u);

    ops = salts_tstr_cmeta_buffer_ops;
    {
        cmeta_type_desc forged = salts_tstr_cmeta_type;
        forged.align += 1u;
        ops.storage_type = &forged;
        data = owned_string;
        data.buffer_ops = &ops;
        check_equal(decode_full(&data, &token, 1u, &out, 0u, 1u, NULL,
                                &source_index), CBIND_INVALID_SHAPE);
        check_equal(source_index, (size_t)0u);
    }

    ops = salts_tstr_cmeta_buffer_ops;
    ops.ownership = CMETA_DATA_BUFFER_CUSTOM;
    shape.ownership = CMETA_DATA_BUFFER_CUSTOM;
    data.shape = &shape;
    data.buffer_ops = &ops;
    check_equal(decode_full(&data, &token, 1u, &out, 0u, 1u, NULL,
                            &source_index), CBIND_UNSUPPORTED);
    check_equal(source_index, (size_t)0u);
  }
}

spec("CBind root buffer decode") {
  it("copies owned string and bytes tokens exactly") {
    static const unsigned char binary[] = {'a', 0, 'b'};
    const cserde_token string_token = TOKEN_SLICE(
        CSERDE_STRING, "hello", 5u, CSERDE_VIEW_TRANSIENT);
    const cserde_token bytes_token = TOKEN_SLICE(
        CSERDE_BYTES, binary, sizeof(binary), CSERDE_VIEW_STABLE);
    tstr string_out = NULL;
    tstr bytes_out = NULL;

    check_equal(decode_full(&owned_string, &string_token, 1u, &string_out,
                            0u, 5u, NULL, NULL), CBIND_OK);
    check_equal(tstr_len(string_out), (size_t)5u);
    check_equal(memcmp(string_out, "hello", 5u), 0);

    check_equal(decode_full(&owned_bytes, &bytes_token, 1u, &bytes_out,
                            0u, sizeof(binary), NULL, NULL), CBIND_OK);
    check_equal(tstr_len(bytes_out), sizeof(binary));
    check_equal(memcmp(bytes_out, binary, sizeof(binary)), 0);
    tstr_freep(&string_out);
    tstr_freep(&bytes_out);
  }

  it("borrows only stable views") {
    static const unsigned char stable[] = "stable";
    const cserde_token stable_token = TOKEN_SLICE(
        CSERDE_STRING, stable, sizeof(stable) - 1u, CSERDE_VIEW_STABLE);
    const cserde_token transient_token = TOKEN_SLICE(
        CSERDE_STRING, "temp", 4u, CSERDE_VIEW_TRANSIENT);
    vstr out = {NULL, 0u};
    cbind_error error = CBIND_ERROR_INIT;

    check_equal(decode_full(&borrowed_string, &stable_token, 1u, &out,
                            0u, sizeof(stable) - 1u, NULL, NULL), CBIND_OK);
    check_true(out.data == (const char *)stable);
    check_equal(out.len, sizeof(stable) - 1u);
    out = (vstr){NULL, 0u};

    check_equal(decode_full(&borrowed_string, &transient_token, 1u, &out,
                            0u, 4u, &error, NULL), CBIND_UNSUPPORTED);
    check_null(out.data);
    check_equal(out.len, (size_t)0u);
    check_true(error.shape == &borrowed_string);
  }

  it("enforces exact token kind, size limit, and empty destination") {
    const cserde_token exact = TOKEN_SLICE(
        CSERDE_BYTES, "1234", 4u, CSERDE_VIEW_TRANSIENT);
    const cserde_token wrong = TOKEN_SLICE(
        CSERDE_STRING, "1234", 4u, CSERDE_VIEW_TRANSIENT);
    const cserde_token empty = TOKEN_SLICE(
        CSERDE_BYTES, NULL, 0u, CSERDE_VIEW_TRANSIENT);
    tstr out = NULL;
    cbind_error error = CBIND_ERROR_INIT;
    size_t source_index = SIZE_MAX;

    check_equal(decode_full(&owned_bytes, &empty, 1u, &out, 0u, 0u,
                            NULL, NULL), CBIND_OK);
    check_null(out);

    check_equal(decode_full(&owned_bytes, &exact, 1u, &out, 0u, 3u,
                            &error, NULL), CBIND_LIMIT_EXCEEDED);
    check_null(out);
    check_equal(error.target_status, CMETA_CAPACITY_EXCEEDED);
    check_equal(decode_full(&owned_bytes, &wrong, 1u, &out, 0u, 4u,
                            NULL, NULL), CBIND_TOKEN_MISMATCH);
    check_null(out);

    out = tstr_dup("occupied");
    check_equal(decode_full(&owned_bytes, &exact, 1u, &out, 0u, 4u,
                            NULL, &source_index),
                CBIND_DESTINATION_NOT_EMPTY);
    check_equal(source_index, (size_t)0u);
    check_equal(out, "occupied");
    tstr_freep(&out);
  }

  it("maps provider allocation failure and preserves semantic zero") {
    const cserde_token token = TOKEN_SLICE(
        CSERDE_BYTES, "x", 1u, CSERDE_VIEW_TRANSIENT);
    cmeta_data_desc data = owned_bytes;
    tstr out = NULL;
    cbind_error error = CBIND_ERROR_INIT;

    data.buffer_ops = &failing_ops;
    check_equal(decode_full(&data, &token, 1u, &out, 0u, 1u,
                            &error, NULL), CBIND_TARGET_ERROR);
    check_equal(error.target_status, CMETA_OUT_OF_MEMORY);
    check_null(out);
  }
}

spec("CBind struct buffer decode") {
  it("decodes owned and borrowed fields with their declared lifetimes") {
    static const unsigned char payload[] = {'x', 0, 'y'};
    static const unsigned char alias[] = "view";
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN,
        TOKEN_KEY("id"), TOKEN_SINT(7),
        TOKEN_KEY("payload"),
        TOKEN_SLICE(CSERDE_BYTES, payload, sizeof(payload),
                    CSERDE_VIEW_TRANSIENT),
        TOKEN_KEY("alias"),
        TOKEN_SLICE(CSERDE_STRING, alias, sizeof(alias) - 1u,
                    CSERDE_VIEW_STABLE),
        TOKEN_MAP_END
    };
    cbind_buffer_record out = {0};

    check_equal(decode_full(&record_data, tokens,
                            sizeof(tokens) / sizeof(tokens[0]), &out,
                            1u, sizeof(alias) - 1u, NULL, NULL), CBIND_OK);
    check_equal(out.id, 7);
    check_equal(tstr_len(out.payload), sizeof(payload));
    check_equal(memcmp(out.payload, payload, sizeof(payload)), 0);
    check_true(out.alias.data == (const char *)alias);
    check_equal(out.alias.len, sizeof(alias) - 1u);
    tstr_freep(&out.payload);
  }

  it("rolls back earlier scalar and owned fields after borrowed failure") {
    static const unsigned char payload[] = {'o', 'k'};
    const cserde_token tokens[] = {
        TOKEN_MAP_BEGIN,
        TOKEN_KEY("id"), TOKEN_SINT(9),
        TOKEN_KEY("payload"),
        TOKEN_SLICE(CSERDE_BYTES, payload, sizeof(payload),
                    CSERDE_VIEW_TRANSIENT),
        TOKEN_KEY("alias"),
        TOKEN_SLICE(CSERDE_STRING, "bad", 3u, CSERDE_VIEW_TRANSIENT)
    };
    cbind_buffer_record out = {0};
    cbind_buffer_record zero = {0};
    cbind_error error = CBIND_ERROR_INIT;

    check_equal(decode_full(&record_data, tokens,
                            sizeof(tokens) / sizeof(tokens[0]), &out,
                            1u, 3u, &error, NULL), CBIND_UNSUPPORTED);
    check_equal(memcmp(&out, &zero, sizeof(out)), 0);
    check_true(error.shape == &borrowed_string);
    check_true(error.field == &record_fields[2]);
    check_equal(error.depth, (size_t)1u);
  }

  it("rolls back earlier fields after a buffer limit or source failure") {
    static const unsigned char payload[] = {'1', '2', '3'};
    const cserde_token over_limit[] = {
        TOKEN_MAP_BEGIN,
        TOKEN_KEY("id"), TOKEN_SINT(4),
        TOKEN_KEY("payload"),
        TOKEN_SLICE(CSERDE_BYTES, payload, sizeof(payload),
                    CSERDE_VIEW_TRANSIENT)
    };
    const cserde_token truncated[] = {
        TOKEN_MAP_BEGIN,
        TOKEN_KEY("id"), TOKEN_SINT(5),
        TOKEN_KEY("payload"),
        TOKEN_SLICE(CSERDE_BYTES, payload, sizeof(payload),
                    CSERDE_VIEW_TRANSIENT)
    };
    cbind_buffer_record out = {0};
    cbind_buffer_record zero = {0};
    cbind_error error = CBIND_ERROR_INIT;

    check_equal(decode_full(&record_data, over_limit,
                            sizeof(over_limit) / sizeof(over_limit[0]), &out,
                            1u, 2u, &error, NULL), CBIND_LIMIT_EXCEEDED);
    check_equal(error.target_status, CMETA_CAPACITY_EXCEEDED);
    check_equal(memcmp(&out, &zero, sizeof(out)), 0);

    error = (cbind_error)CBIND_ERROR_INIT;
    check_equal(decode_full(&record_data, truncated,
                            sizeof(truncated) / sizeof(truncated[0]), &out,
                            1u, sizeof(payload), &error, NULL),
                CBIND_UNEXPECTED_END);
    check_equal(error.source_status, CSERDE_DONE);
    check_equal(memcmp(&out, &zero, sizeof(out)), 0);
  }
}

#undef TOKEN_KEY
#undef TOKEN_SLICE
#undef TOKEN_SINT
#undef TOKEN_MAP_END
#undef TOKEN_MAP_BEGIN
