#include <cbind/cbind.h>
#include "recording.h"
#include "tinytest.h"

#include <float.h>
#include <limits.h>
#include <math.h>
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

#define DEFINE_FIXED_INTEGER_DATA(name_, c_type_, kind_, bits_, id_)         \
    static const cmeta_type_identity name_##_identity =                     \
        CMETA_TYPE_ID_ATOM_INIT(id_);                                        \
    static const cmeta_type_desc name_##_type = {                            \
        .name = #c_type_,                                                    \
        .size = sizeof(c_type_),                                             \
        .align = _Alignof(c_type_),                                          \
        .kind = CMETA_T_INTEGER,                                             \
        .pointee = NULL,                                                     \
        .traits = NULL,                                                      \
        .identity = &name_##_identity                                        \
    };                                                                       \
    static const cmeta_data_integer_shape name_##_shape = {.bits = bits_};   \
    static const cmeta_data_desc name_##_data = {                            \
        .struct_size = offsetof(cmeta_data_desc, shape) +                    \
                       sizeof(((cmeta_data_desc *)0)->shape),                 \
        .abi_version = CMETA_DATA_DESC_ABI_VERSION,                          \
        .stable_id = id_ ".data",                                           \
        .display_name = #c_type_,                                            \
        .kind = kind_,                                                       \
        .storage_type = &name_##_type,                                       \
        .shape = &name_##_shape                                              \
    }

DEFINE_FIXED_INTEGER_DATA(test_i8, int8_t, CMETA_DATA_SINT, 8u,
                          "test.cbind.int8");
DEFINE_FIXED_INTEGER_DATA(test_i16, int16_t, CMETA_DATA_SINT, 16u,
                          "test.cbind.int16");
DEFINE_FIXED_INTEGER_DATA(test_i32, int32_t, CMETA_DATA_SINT, 32u,
                          "test.cbind.int32");
DEFINE_FIXED_INTEGER_DATA(test_i64, int64_t, CMETA_DATA_SINT, 64u,
                          "test.cbind.int64");
DEFINE_FIXED_INTEGER_DATA(test_u8, uint8_t, CMETA_DATA_UINT, 8u,
                          "test.cbind.uint8");
DEFINE_FIXED_INTEGER_DATA(test_u16, uint16_t, CMETA_DATA_UINT, 16u,
                          "test.cbind.uint16");
DEFINE_FIXED_INTEGER_DATA(test_u32, uint32_t, CMETA_DATA_UINT, 32u,
                          "test.cbind.uint32");
DEFINE_FIXED_INTEGER_DATA(test_u64, uint64_t, CMETA_DATA_UINT, 64u,
                          "test.cbind.uint64");

#undef DEFINE_FIXED_INTEGER_DATA

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
                CBIND_OK);
    check_equal(source.index, (size_t)1u);
    check_equal(out, 1);
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

spec("CBind fixed-width integer decode") {
  it("decodes every signed fixed-width minimum and maximum") {
    cserde_token token = {.kind = CSERDE_SINT};
    int8_t i8 = 0;
    int16_t i16 = 0;
    int32_t i32 = 0;
    int64_t i64 = 0;

    token.value.sint = INT8_MIN;
    check_equal(decode_one(&test_i8_data, &token, &i8), CBIND_OK);
    check_equal(i8, INT8_MIN);
    i8 = 0;
    token.value.sint = INT8_MAX;
    check_equal(decode_one(&test_i8_data, &token, &i8), CBIND_OK);
    check_equal(i8, INT8_MAX);

    token.value.sint = INT16_MIN;
    check_equal(decode_one(&test_i16_data, &token, &i16), CBIND_OK);
    check_equal(i16, INT16_MIN);
    i16 = 0;
    token.value.sint = INT16_MAX;
    check_equal(decode_one(&test_i16_data, &token, &i16), CBIND_OK);
    check_equal(i16, INT16_MAX);

    token.value.sint = INT32_MIN;
    check_equal(decode_one(&test_i32_data, &token, &i32), CBIND_OK);
    check_equal(i32, INT32_MIN);
    i32 = 0;
    token.value.sint = INT32_MAX;
    check_equal(decode_one(&test_i32_data, &token, &i32), CBIND_OK);
    check_equal(i32, INT32_MAX);

    token.value.sint = INT64_MIN;
    check_equal(decode_one(&test_i64_data, &token, &i64), CBIND_OK);
    check_equal(i64, INT64_MIN);
    i64 = 0;
    token.value.sint = INT64_MAX;
    check_equal(decode_one(&test_i64_data, &token, &i64), CBIND_OK);
    check_equal(i64, INT64_MAX);
  }

  it("accepts unsigned tokens within signed fixed-width ranges") {
    cserde_token token = {.kind = CSERDE_UINT};
    int8_t i8 = 0;
    int16_t i16 = 0;
    int32_t i32 = 0;
    int64_t i64 = 0;

    token.value.uint = INT8_MAX;
    check_equal(decode_one(&test_i8_data, &token, &i8), CBIND_OK);
    check_equal(i8, INT8_MAX);
    token.value.uint = INT16_MAX;
    check_equal(decode_one(&test_i16_data, &token, &i16), CBIND_OK);
    check_equal(i16, INT16_MAX);
    token.value.uint = INT32_MAX;
    check_equal(decode_one(&test_i32_data, &token, &i32), CBIND_OK);
    check_equal(i32, INT32_MAX);
    token.value.uint = INT64_MAX;
    check_equal(decode_one(&test_i64_data, &token, &i64), CBIND_OK);
    check_equal(i64, INT64_MAX);
  }

  it("rejects one-past signed fixed-width ranges without changing output") {
    cserde_token token = {.kind = CSERDE_SINT};
    int8_t i8 = 0;
    int16_t i16 = 0;
    int32_t i32 = 0;
    int64_t i64 = 0;

    token.value.sint = (int64_t)INT8_MAX + 1;
    check_equal(decode_one(&test_i8_data, &token, &i8),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(i8, 0);
    token.value.sint = (int64_t)INT16_MIN - 1;
    check_equal(decode_one(&test_i16_data, &token, &i16),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(i16, 0);
    token.value.sint = (int64_t)INT32_MAX + 1;
    check_equal(decode_one(&test_i32_data, &token, &i32),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(i32, 0);

    token.kind = CSERDE_UINT;
    token.value.uint = (uint64_t)INT64_MAX + UINT64_C(1);
    check_equal(decode_one(&test_i64_data, &token, &i64),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(i64, INT64_C(0));
  }

  it("decodes every unsigned fixed-width maximum and signed positive token") {
    cserde_token token = {.kind = CSERDE_UINT};
    uint8_t u8 = 0;
    uint16_t u16 = 0;
    uint32_t u32 = 0;
    uint64_t u64 = 0;

    token.value.uint = UINT8_MAX;
    check_equal(decode_one(&test_u8_data, &token, &u8), CBIND_OK);
    check_equal(u8, UINT8_MAX);
    token.value.uint = UINT16_MAX;
    check_equal(decode_one(&test_u16_data, &token, &u16), CBIND_OK);
    check_equal(u16, UINT16_MAX);
    token.value.uint = UINT32_MAX;
    check_equal(decode_one(&test_u32_data, &token, &u32), CBIND_OK);
    check_equal(u32, UINT32_MAX);
    token.value.uint = UINT64_MAX;
    check_equal(decode_one(&test_u64_data, &token, &u64), CBIND_OK);
    check_equal(u64, UINT64_MAX);

    u8 = 0;
    token.kind = CSERDE_SINT;
    token.value.sint = INT8_MAX;
    check_equal(decode_one(&test_u8_data, &token, &u8), CBIND_OK);
    check_equal(u8, (uint8_t)INT8_MAX);
  }

  it("rejects negative and one-past unsigned ranges without changing output") {
    cserde_token token = {.kind = CSERDE_SINT, .value.sint = -1};
    uint8_t u8 = 0;
    uint16_t u16 = 0;
    uint32_t u32 = 0;
    uint64_t u64 = 0;

    check_equal(decode_one(&test_u64_data, &token, &u64),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(u64, UINT64_C(0));

    token.kind = CSERDE_UINT;
    token.value.uint = (uint64_t)UINT8_MAX + UINT64_C(1);
    check_equal(decode_one(&test_u8_data, &token, &u8),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(u8, 0);
    token.value.uint = (uint64_t)UINT16_MAX + UINT64_C(1);
    check_equal(decode_one(&test_u16_data, &token, &u16),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(u16, 0);
    token.value.uint = (uint64_t)UINT32_MAX + UINT64_C(1);
    check_equal(decode_one(&test_u32_data, &token, &u32),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(u32, 0u);
  }

  it("accepts integral floats and rejects fractional or nonfinite values") {
    cserde_token token = {.kind = CSERDE_FLOAT, .value.floating = 42.0};
    int8_t i8 = 0;
    int16_t i16 = 0;
    uint32_t u32 = 0;
    uint64_t u64 = 0;

    check_equal(decode_one(&test_i8_data, &token, &i8), CBIND_OK);
    check_equal(i8, 42);
    check_equal(decode_one(&test_i16_data, &token, &i16), CBIND_OK);
    check_equal(i16, 42);
    check_equal(decode_one(&test_u32_data, &token, &u32), CBIND_OK);
    check_equal(u32, 42u);
    check_equal(decode_one(&test_u64_data, &token, &u64), CBIND_OK);
    check_equal(u64, UINT64_C(42));

    i8 = 0;
    token.value.floating = 42.5;
    check_equal(decode_one(&test_i8_data, &token, &i8),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(i8, 0);
    token.value.floating = NAN;
    check_equal(decode_one(&test_i8_data, &token, &i8),
                CBIND_VALUE_OUT_OF_RANGE);
    token.value.floating = INFINITY;
    check_equal(decode_one(&test_i8_data, &token, &i8),
                CBIND_VALUE_OUT_OF_RANGE);
  }

  it("rejects wrong integer bits size alignment and storage kind before input") {
    cmeta_data_integer_shape bad_bits = {.bits = 7u};
    cmeta_type_desc bad_size_type = test_i8_type;
    cmeta_type_desc bad_align_type = test_i8_type;
    cmeta_type_desc bad_kind_type = test_i8_type;
    cmeta_data_desc bad_bits_data = test_i8_data;
    cmeta_data_desc bad_size_data = test_i8_data;
    cmeta_data_desc bad_align_data = test_i8_data;
    cmeta_data_desc bad_kind_data = test_i8_data;
    cserde_token token = {.kind = CSERDE_SINT, .value.sint = 1};
    cserde_recording_reader_context source;
    cserde_reader reader;
    cbind_context context = CBIND_CONTEXT_INIT(NULL, 0u, 0u);
    int8_t out = 0;

    bad_bits_data.shape = &bad_bits;
    bad_size_type.size = sizeof(int16_t);
    bad_size_data.storage_type = &bad_size_type;
    bad_align_type.align = _Alignof(int8_t) + 1u;
    bad_align_data.storage_type = &bad_align_type;
    bad_kind_type.kind = CMETA_T_OBJECT;
    bad_kind_data.storage_type = &bad_kind_type;

    init_recording_reader(&reader, &source, &token, 1u);
    check_equal(cbind_decode(&context, &bad_bits_data, &reader, &out, NULL),
                CBIND_INVALID_SHAPE);
    check_equal(source.index, (size_t)0u);
    check_equal(cbind_decode(&context, &bad_size_data, &reader, &out, NULL),
                CBIND_INVALID_SHAPE);
    check_equal(source.index, (size_t)0u);
    check_equal(cbind_decode(&context, &bad_align_data, &reader, &out, NULL),
                CBIND_INVALID_SHAPE);
    check_equal(source.index, (size_t)0u);
    check_equal(cbind_decode(&context, &bad_kind_data, &reader, &out, NULL),
                CBIND_INVALID_SHAPE);
    check_equal(source.index, (size_t)0u);
    check_equal(out, 0);
  }

  it("detects occupied narrow destinations without reading adjacent bytes") {
    struct guarded_i8 {
      int8_t value;
      unsigned char guard[7];
    } out = {1, {0}};
    cserde_token token = {.kind = CSERDE_SINT, .value.sint = 2};

    check_equal(decode_one(&test_i8_data, &token, &out.value),
                CBIND_DESTINATION_NOT_EMPTY);
    check_equal(out.value, 1);
    check_equal(out.guard[0], 0);
    check_equal(out.guard[6], 0);
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

spec("CBind strict floating conversion") {
  it("accepts integral finite float for integer and rejects fractional/nonfinite") {
    cserde_token token = {.kind = CSERDE_FLOAT};
    int out = 0;

    token.value.floating = 42.0;
    check_equal(decode_one(&cmeta_data_int, &token, &out), CBIND_OK);
    check_equal(out, 42);

    out = 0;
    token.value.floating = 42.5;
    check_equal(decode_one(&cmeta_data_int, &token, &out),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(out, 0);

    token.value.floating = NAN;
    check_equal(decode_one(&cmeta_data_int, &token, &out),
                CBIND_VALUE_OUT_OF_RANGE);
    token.value.floating = INFINITY;
    check_equal(decode_one(&cmeta_data_int, &token, &out),
                CBIND_VALUE_OUT_OF_RANGE);
  }

  it("checks float to signed and unsigned bounds before casting") {
    cserde_token token = {.kind = CSERDE_FLOAT};
    int signed_out = 0;
    size_t unsigned_out = 0u;

    token.value.floating = (double)INT_MIN;
    check_equal(decode_one(&cmeta_data_int, &token, &signed_out), CBIND_OK);
    check_equal(signed_out, INT_MIN);

    signed_out = 0;
    token.value.floating = ldexp(1.0, (int)(sizeof(int) * CHAR_BIT - 1u));
    check_equal(decode_one(&cmeta_data_int, &token, &signed_out),
                CBIND_VALUE_OUT_OF_RANGE);

    token.value.floating = -1.0;
    check_equal(decode_one(&cmeta_data_size, &token, &unsigned_out),
                CBIND_VALUE_OUT_OF_RANGE);
  }

  it("preserves canonical nonfinite values when decoding double") {
    cserde_token token = {.kind = CSERDE_FLOAT, .value.floating = NAN};
    double out = 0.0;

    check_equal(decode_one(&cmeta_data_double, &token, &out), CBIND_OK);
    check_true(isnan(out));

    out = 0.0;
    token.value.floating = INFINITY;
    check_equal(decode_one(&cmeta_data_double, &token, &out), CBIND_OK);
    check_true(isinf(out) && out > 0.0);
  }

  it("rejects finite float32 overflow and nonzero underflow but allows rounding") {
    cserde_token token = {.kind = CSERDE_FLOAT};
    float out = 0.0f;

    token.value.floating = DBL_MAX;
    check_equal(decode_one(&cmeta_data_float, &token, &out),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(out, 0.0f);

    token.value.floating = DBL_TRUE_MIN;
    check_equal(decode_one(&cmeta_data_float, &token, &out),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(out, 0.0f);

    token.value.floating = 1.0 + DBL_EPSILON;
    check_equal(decode_one(&cmeta_data_float, &token, &out), CBIND_OK);
    check_equal(out, 1.0f);
  }

  it("requires exact integer representability in binary32") {
    cserde_token token = {.kind = CSERDE_SINT};
    float out = 0.0f;

    token.value.sint = INT64_C(1) << 24;
    check_equal(decode_one(&cmeta_data_float, &token, &out), CBIND_OK);
    check_equal(out, 16777216.0f);

    out = 0.0f;
    token.value.sint = (INT64_C(1) << 24) + 1;
    check_equal(decode_one(&cmeta_data_float, &token, &out),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(out, 0.0f);
  }

  it("requires exact integer representability in binary64") {
    cserde_token token = {.kind = CSERDE_SINT};
    double out = 0.0;

    token.value.sint = INT64_C(1) << 53;
    check_equal(decode_one(&cmeta_data_double, &token, &out), CBIND_OK);
    check_equal(out, 9007199254740992.0);

    out = 0.0;
    token.value.sint = (INT64_C(1) << 53) + 1;
    check_equal(decode_one(&cmeta_data_double, &token, &out),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(out, 0.0);

    token.value.sint = INT64_MIN;
    check_equal(decode_one(&cmeta_data_double, &token, &out), CBIND_OK);
    check_equal(out, -9223372036854775808.0);
  }

  it("rejects inexact uint64 to double") {
    cserde_token token = {.kind = CSERDE_UINT, .value.uint = UINT64_MAX};
    double out = 0.0;

    check_equal(decode_one(&cmeta_data_double, &token, &out),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(out, 0.0);
  }
}
