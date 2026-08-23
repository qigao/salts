#include <cbind/cbind.h>
#include "recording.h"
#include "tinytest.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

static void init_recording_reader(cserde_reader *reader,
                                  cserde_recording_reader_context *context,
                                  const cserde_token *tokens,
                                  size_t count) {
    *reader = (cserde_reader){0};
    *context = (cserde_recording_reader_context){tokens, count, 0u};
    check_equal(cserde_reader_init(reader, &cserde_recording_reader_ops, context),
                CSERDE_OK);
}

static cbind_status decode_one(const cmeta_data_desc *shape,
                               const cserde_token *token,
                               void *out) {
    cserde_recording_reader_context source;
    cserde_reader reader;
    cbind_context context = CBIND_CONTEXT_INIT(NULL, 0u, 0u);
    cbind_error error = CBIND_ERROR_INIT;

    init_recording_reader(&reader, &source, token, 1u);
    return cbind_decode(&context, shape, &reader, out, &error);
}

static const cmeta_type_identity noncanonical_int_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.cbind.noncanonical-int");
static const cmeta_type_desc noncanonical_int_type = {
    .name = "noncanonical_int",
    .size = sizeof(int),
    .align = _Alignof(int),
    .kind = CMETA_T_INTEGER,
    .pointee = NULL,
    .traits = NULL,
    .identity = &noncanonical_int_identity
};
static const cmeta_data_integer_shape noncanonical_int_shape = {
    .bits = (uint8_t)(sizeof(int) * CHAR_BIT)
};
static const cmeta_data_desc noncanonical_int_data = {
    .struct_size = offsetof(cmeta_data_desc, shape) +
                   sizeof(((cmeta_data_desc *)0)->shape),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.noncanonical-int.data",
    .display_name = "noncanonical int",
    .kind = CMETA_DATA_SINT,
    .storage_type = &noncanonical_int_type,
    .shape = &noncanonical_int_shape
};

spec("CBind public ABI records") {
  it("initializes a caller-sized context") {
    unsigned char scratch[3] = {0};
    cbind_context context = CBIND_CONTEXT_INIT(scratch, sizeof(scratch), 2u);

    check_equal(context.struct_size, sizeof(cbind_context));
    check_equal(context.abi_version, (uint32_t)CBIND_CONTEXT_ABI_VERSION);
    check_true(context.scratch == scratch);
    check_equal(context.scratch_size, sizeof(scratch));
    check_equal(context.max_depth, (size_t)2u);
  }

  it("initializes a caller-sized error record") {
    cbind_error error = CBIND_ERROR_INIT;

    check_equal(error.struct_size, sizeof(cbind_error));
    check_equal(error.abi_version, (uint32_t)CBIND_ERROR_ABI_VERSION);
    check_equal(error.status, CBIND_OK);
    check_equal(error.source_status, CSERDE_OK);
    check_null(error.shape);
    check_null(error.field);
    check_equal(error.depth, (size_t)0u);
  }

  it("uses stable v1 enum and record ABI versions") {
    check_equal(CBIND_OK, 0);
    check_equal(CBIND_CONTEXT_ABI_VERSION, 1u);
    check_equal(CBIND_ERROR_ABI_VERSION, 1u);
  }
}

spec("CBind scalar decode preflight") {
  it("rejects required null arguments before consuming input") {
    cserde_token token = {.kind = CSERDE_SINT, .value.sint = 1};
    cserde_recording_reader_context source;
    cserde_reader reader;
    cbind_context context = CBIND_CONTEXT_INIT(NULL, 0u, 0u);
    int out = 0;

    init_recording_reader(&reader, &source, &token, 1u);
    check_equal(cbind_decode(&context, NULL, &reader, &out, NULL),
                CBIND_INVALID_ARGUMENT);
    check_equal(source.index, (size_t)0u);
    check_equal(cbind_decode(&context, &cmeta_data_int, NULL, &out, NULL),
                CBIND_INVALID_ARGUMENT);
    check_equal(source.index, (size_t)0u);
    check_equal(cbind_decode(&context, &cmeta_data_int, &reader, NULL, NULL),
                CBIND_INVALID_ARGUMENT);
    check_equal(source.index, (size_t)0u);
  }

  it("rejects invalid context records before consuming input") {
    cserde_token token = {.kind = CSERDE_SINT, .value.sint = 1};
    cserde_recording_reader_context source;
    cserde_reader reader;
    cbind_context context = CBIND_CONTEXT_INIT(NULL, 0u, 0u);
    int out = 0;

    init_recording_reader(&reader, &source, &token, 1u);
    check_equal(cbind_decode(NULL, &cmeta_data_int, &reader, &out, NULL),
                CBIND_INVALID_CONTEXT);
    check_equal(source.index, (size_t)0u);

    context.struct_size = offsetof(cbind_context, max_depth) +
                          sizeof(context.max_depth) - 1u;
    check_equal(cbind_decode(&context, &cmeta_data_int, &reader, &out, NULL),
                CBIND_INVALID_CONTEXT);
    check_equal(source.index, (size_t)0u);

    context = (cbind_context)CBIND_CONTEXT_INIT(NULL, 0u, 0u);
    context.abi_version += 1u;
    check_equal(cbind_decode(&context, &cmeta_data_int, &reader, &out, NULL),
                CBIND_INVALID_CONTEXT);
    check_equal(source.index, (size_t)0u);

    context = (cbind_context)CBIND_CONTEXT_INIT(NULL, 1u, 0u);
    check_equal(cbind_decode(&context, &cmeta_data_int, &reader, &out, NULL),
                CBIND_INVALID_CONTEXT);
    check_equal(source.index, (size_t)0u);
  }

  it("rejects an invalid error record before consuming input") {
    cserde_token token = {.kind = CSERDE_SINT, .value.sint = 1};
    cserde_recording_reader_context source;
    cserde_reader reader;
    cbind_context context = CBIND_CONTEXT_INIT(NULL, 0u, 0u);
    cbind_error error = CBIND_ERROR_INIT;
    int out = 0;

    init_recording_reader(&reader, &source, &token, 1u);
    error.struct_size = offsetof(cbind_error, depth) + sizeof(error.depth) - 1u;
    check_equal(cbind_decode(&context, &cmeta_data_int, &reader, &out, &error),
                CBIND_INVALID_ARGUMENT);
    check_equal(source.index, (size_t)0u);
  }

  it("rejects unsupported and malformed scalar shapes before input") {
    static const cmeta_data_buffer_shape owned = {
        .ownership = CMETA_DATA_BUFFER_OWNED
    };
    cmeta_data_desc string_data = {
        .struct_size = offsetof(cmeta_data_desc, shape) +
                       sizeof(((cmeta_data_desc *)0)->shape),
        .abi_version = CMETA_DATA_DESC_ABI_VERSION,
        .stable_id = "test.cbind.string",
        .display_name = "string",
        .kind = CMETA_DATA_STRING,
        .storage_type = &cmeta_type_int,
        .shape = &owned
    };
    cmeta_data_integer_shape forged_shape = {
        .bits = (uint8_t)(sizeof(int) * CHAR_BIT == 64 ? 32 : 64)
    };
    cmeta_data_desc forged_int = cmeta_data_int;
    cserde_token token = {.kind = CSERDE_SINT, .value.sint = 1};
    cserde_recording_reader_context source;
    cserde_reader reader;
    cbind_context context = CBIND_CONTEXT_INIT(NULL, 0u, 0u);
    int out = 0;

    check_true(cmeta_data_desc_valid(&string_data));
    check_true(cmeta_data_desc_valid(&noncanonical_int_data));
    forged_int.shape = &forged_shape;
    check_true(cmeta_data_desc_valid(&forged_int));

    init_recording_reader(&reader, &source, &token, 1u);
    check_equal(cbind_decode(&context, &string_data, &reader, &out, NULL),
                CBIND_UNSUPPORTED);
    check_equal(source.index, (size_t)0u);
    check_equal(cbind_decode(&context, &forged_int, &reader, &out, NULL),
                CBIND_INVALID_SHAPE);
    check_equal(source.index, (size_t)0u);
    check_equal(cbind_decode(&context, &noncanonical_int_data, &reader, &out, NULL),
                CBIND_UNSUPPORTED);
    check_equal(source.index, (size_t)0u);
  }

  it("rejects a non-empty destination before input") {
    cserde_token token = {.kind = CSERDE_SINT, .value.sint = 1};
    cserde_recording_reader_context source;
    cserde_reader reader;
    cbind_context context = CBIND_CONTEXT_INIT(NULL, 0u, 0u);
    int out = 9;

    init_recording_reader(&reader, &source, &token, 1u);
    check_equal(cbind_decode(&context, &cmeta_data_int, &reader, &out, NULL),
                CBIND_DESTINATION_NOT_EMPTY);
    check_equal(source.index, (size_t)0u);
    check_equal(out, 9);
  }
}

spec("CBind canonical bool and integer decode") {
  it("decodes bool true and false and rejects numeric bool coercion") {
    cserde_token true_token = {.kind = CSERDE_BOOL, .value.boolean = true};
    cserde_token false_token = {.kind = CSERDE_BOOL, .value.boolean = false};
    cserde_token sint_token = {.kind = CSERDE_SINT, .value.sint = 1};
    _Bool out = 0;

    check_equal(decode_one(&cmeta_data_bool, &true_token, &out), CBIND_OK);
    check_true(out);
    out = 0;
    check_equal(decode_one(&cmeta_data_bool, &false_token, &out), CBIND_OK);
    check_false(out);
    out = 0;
    check_equal(decode_one(&cmeta_data_bool, &sint_token, &out),
                CBIND_TOKEN_MISMATCH);
    check_false(out);
  }

  it("decodes signed int and long native boundaries") {
    cserde_token token = {.kind = CSERDE_SINT};
    int int_out = 0;
    long long_out = 0;

    token.value.sint = (int64_t)INT_MIN;
    check_equal(decode_one(&cmeta_data_int, &token, &int_out), CBIND_OK);
    check_equal(int_out, INT_MIN);
    int_out = 0;
    token.value.sint = (int64_t)INT_MAX;
    check_equal(decode_one(&cmeta_data_int, &token, &int_out), CBIND_OK);
    check_equal(int_out, INT_MAX);

    token.value.sint = (int64_t)LONG_MIN;
    check_equal(decode_one(&cmeta_data_long, &token, &long_out), CBIND_OK);
    check_equal(long_out, LONG_MIN);
    long_out = 0;
    token.value.sint = (int64_t)LONG_MAX;
    check_equal(decode_one(&cmeta_data_long, &token, &long_out), CBIND_OK);
    check_equal(long_out, LONG_MAX);
  }

  it("checks unsigned to signed range exactly") {
    cserde_token token = {.kind = CSERDE_UINT};
    int out = 0;

    token.value.uint = (uint64_t)INT_MAX;
    check_equal(decode_one(&cmeta_data_int, &token, &out), CBIND_OK);
    check_equal(out, INT_MAX);
    out = 0;
    token.value.uint = (uint64_t)INT_MAX + UINT64_C(1);
    check_equal(decode_one(&cmeta_data_int, &token, &out),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(out, 0);
  }

  it("decodes size_t max and rejects negative signed input") {
    cserde_token token = {.kind = CSERDE_UINT};
    size_t out = 0u;

    token.value.uint = (uint64_t)SIZE_MAX;
    check_equal(decode_one(&cmeta_data_size, &token, &out), CBIND_OK);
    check_equal(out, SIZE_MAX);
    out = 0u;
    token.kind = CSERDE_SINT;
    token.value.sint = -1;
    check_equal(decode_one(&cmeta_data_size, &token, &out),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(out, (size_t)0u);
  }

  it("distinguishes token mismatch from numeric range failure") {
    static const unsigned char text[] = {'1'};
    cserde_token token = {
        .kind = CSERDE_STRING,
        .value.slice = {text, sizeof(text), CSERDE_VIEW_STABLE}
    };
    int out = 0;

    check_equal(decode_one(&cmeta_data_int, &token, &out),
                CBIND_TOKEN_MISMATCH);
    check_equal(out, 0);
  }
}
