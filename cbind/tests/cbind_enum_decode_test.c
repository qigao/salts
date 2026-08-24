#include <cbind/cbind.h>
#include "recording.h"
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

Enum(cbind_test_state,
    (CBIND_TEST_IDLE, 1, "idle"),
    (CBIND_TEST_READY, 2, "ready"),
    (CBIND_TEST_PAUSED, -3, "paused")
);

static const cmeta_type_identity cbind_test_state_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.cbind.State");
static const cmeta_type_desc cbind_test_state_type = {
    .name = "cbind_test_state",
    .size = sizeof(cbind_test_state),
    .align = _Alignof(cbind_test_state),
    .kind = CMETA_T_INTEGER,
    .pointee = NULL,
    .traits = NULL,
    .identity = &cbind_test_state_identity
};

static bool cbind_test_enum_assign_fails;

static bool cbind_test_enum_is_zero(const void *object) {
    cbind_test_state value;
    if (object == NULL)
        return false;
    memcpy(&value, object, sizeof(value));
    return CMETA_ENUM_TO_INT64(value) == 0;
}

static cmeta_status cbind_test_enum_read(const void *object, int64_t *out) {
    cbind_test_state value;
    if (object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    memcpy(&value, object, sizeof(value));
    *out = CMETA_ENUM_TO_INT64(value);
    return CMETA_OK;
}

static cmeta_status cbind_test_enum_assign(void *object, int64_t value) {
    cbind_test_state native;
    if (object == NULL)
        return CMETA_INVALID_ARGUMENT;
    native = CMETA_ENUM_FROM_INT64(cbind_test_state, value);
    memcpy(object, &native, sizeof(native));
    return cbind_test_enum_assign_fails ? CMETA_CALLBACK_ERROR : CMETA_OK;
}

static void cbind_test_enum_restore_zero(void *object) {
    cbind_test_state value = CMETA_ENUM_FROM_INT64(cbind_test_state, 0);
    if (object != NULL)
        memcpy(object, &value, sizeof(value));
}

static const cmeta_data_enum_shape cbind_test_state_shape = {
    .meta = EnumMeta(cbind_test_state)
};
static const cmeta_data_enum_ops cbind_test_state_ops = {
    .struct_size = sizeof(cmeta_data_enum_ops),
    .abi_version = CMETA_DATA_ENUM_OPS_ABI_VERSION,
    .storage_type = &cbind_test_state_type,
    .is_zero = cbind_test_enum_is_zero,
    .read = cbind_test_enum_read,
    .assign = cbind_test_enum_assign,
    .restore_zero = cbind_test_enum_restore_zero
};
static const cmeta_data_desc cbind_test_state_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cbind.State.data",
    .display_name = "State",
    .kind = CMETA_DATA_ENUM,
    .storage_type = &cbind_test_state_type,
    .shape = &cbind_test_state_shape,
    .enum_ops = &cbind_test_state_ops
};

static cbind_status decode_token(const cmeta_data_desc *shape,
                                 const cserde_token *token,
                                 cbind_test_state *out,
                                 size_t *consumed,
                                 cbind_error *error) {
    cserde_recording_reader_context source = {token, 1u, 0u};
    cserde_reader reader = {0};
    cbind_context context = CBIND_CONTEXT_INIT(NULL, 0u, 0u);
    cbind_status status;

    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops,
                                   &source), CSERDE_OK);
    status = cbind_decode(&context, shape, &reader, out, error);
    if (consumed != NULL)
        *consumed = source.index;
    return status;
}

spec("CBind enum decode") {
  before_each() {
    cbind_test_enum_assign_fails = false;
  }

  it("decodes text symbol signed and unsigned exact forms") {
    static const unsigned char ready_text[] = {'r', 'e', 'a', 'd', 'y'};
    static const unsigned char paused_symbol[] = {
        'C', 'B', 'I', 'N', 'D', '_', 'T', 'E', 'S', 'T', '_',
        'P', 'A', 'U', 'S', 'E', 'D'};
    cserde_token token = {
        .kind = CSERDE_STRING,
        .value.slice = {ready_text, sizeof(ready_text), CSERDE_VIEW_STABLE}
    };
    cbind_test_state out = CMETA_ENUM_FROM_INT64(cbind_test_state, 0);

    check_equal(decode_token(&cbind_test_state_data, &token, &out, NULL, NULL),
                CBIND_OK);
    check_equal(CMETA_ENUM_TO_INT64(out), (int64_t)CBIND_TEST_READY);

    out = CMETA_ENUM_FROM_INT64(cbind_test_state, 0);
    token.value.slice = (cserde_slice){
        paused_symbol, sizeof(paused_symbol), CSERDE_VIEW_TRANSIENT};
    check_equal(decode_token(&cbind_test_state_data, &token, &out, NULL, NULL),
                CBIND_OK);
    check_equal(CMETA_ENUM_TO_INT64(out), (int64_t)CBIND_TEST_PAUSED);

    out = CMETA_ENUM_FROM_INT64(cbind_test_state, 0);
    token.kind = CSERDE_SINT;
    token.value.sint = CBIND_TEST_IDLE;
    check_equal(decode_token(&cbind_test_state_data, &token, &out, NULL, NULL),
                CBIND_OK);
    check_equal(CMETA_ENUM_TO_INT64(out), (int64_t)CBIND_TEST_IDLE);

    out = CMETA_ENUM_FROM_INT64(cbind_test_state, 0);
    token.kind = CSERDE_UINT;
    token.value.uint = CBIND_TEST_READY;
    check_equal(decode_token(&cbind_test_state_data, &token, &out, NULL, NULL),
                CBIND_OK);
    check_equal(CMETA_ENUM_TO_INT64(out), (int64_t)CBIND_TEST_READY);
  }

  it("rejects unknown and oversized values and restores zero") {
    static const unsigned char unknown[] = {'m', 'i', 's', 's', 'i', 'n', 'g'};
    cserde_token token = {
        .kind = CSERDE_STRING,
        .value.slice = {unknown, sizeof(unknown), CSERDE_VIEW_STABLE}
    };
    cbind_test_state out = CMETA_ENUM_FROM_INT64(cbind_test_state, 0);

    check_equal(decode_token(&cbind_test_state_data, &token, &out, NULL, NULL),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(CMETA_ENUM_TO_INT64(out), INT64_C(0));

    token.kind = CSERDE_SINT;
    token.value.sint = 99;
    check_equal(decode_token(&cbind_test_state_data, &token, &out, NULL, NULL),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(CMETA_ENUM_TO_INT64(out), INT64_C(0));

    token.kind = CSERDE_UINT;
    token.value.uint = UINT64_MAX;
    check_equal(decode_token(&cbind_test_state_data, &token, &out, NULL, NULL),
                CBIND_VALUE_OUT_OF_RANGE);
    check_equal(CMETA_ENUM_TO_INT64(out), INT64_C(0));
  }

  it("distinguishes wrong tokens from unknown values") {
    cserde_token token = {.kind = CSERDE_FLOAT, .value.floating = 2.0};
    cbind_test_state out = CMETA_ENUM_FROM_INT64(cbind_test_state, 0);

    check_equal(decode_token(&cbind_test_state_data, &token, &out, NULL, NULL),
                CBIND_TOKEN_MISMATCH);
    check_equal(CMETA_ENUM_TO_INT64(out), INT64_C(0));
  }

  it("reports provider assignment failure and rolls back") {
    cserde_token token = {.kind = CSERDE_SINT, .value.sint = CBIND_TEST_READY};
    cbind_test_state out = CMETA_ENUM_FROM_INT64(cbind_test_state, 0);
    cbind_error error = CBIND_ERROR_INIT;

    cbind_test_enum_assign_fails = true;
    check_equal(decode_token(&cbind_test_state_data, &token, &out, NULL, &error),
                CBIND_TARGET_ERROR);
    check_equal(error.target_status, CMETA_CALLBACK_ERROR);
    check_true(error.shape == &cbind_test_state_data);
    check_equal(CMETA_ENUM_TO_INT64(out), INT64_C(0));
  }

  it("rejects missing adapters and non-empty destinations before input") {
    cmeta_data_desc missing_ops = cbind_test_state_data;
    cserde_token token = {.kind = CSERDE_SINT, .value.sint = CBIND_TEST_READY};
    cbind_test_state out = CMETA_ENUM_FROM_INT64(cbind_test_state, 0);
    size_t consumed = 99u;

    missing_ops.struct_size = offsetof(cmeta_data_desc, shape) +
                              sizeof(missing_ops.shape);
    missing_ops.enum_ops = NULL;
    check_equal(decode_token(&missing_ops, &token, &out, &consumed, NULL),
                CBIND_UNSUPPORTED);
    check_equal(consumed, (size_t)0u);

    out = CBIND_TEST_IDLE;
    check_equal(decode_token(&cbind_test_state_data, &token, &out,
                             &consumed, NULL),
                CBIND_DESTINATION_NOT_EMPTY);
    check_equal(consumed, (size_t)0u);
    check_equal(CMETA_ENUM_TO_INT64(out), (int64_t)CBIND_TEST_IDLE);
  }
}
