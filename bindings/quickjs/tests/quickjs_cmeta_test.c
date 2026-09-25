#include <salts/bindings/quickjs/cmeta.h>

#include <cmeta/interface.h>
#include <cstl/typed.h>
#include "tinytest.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct quickjs_test_buffer {
  unsigned char *data;
  size_t size;
} quickjs_test_buffer;

static size_t quickjs_test_buffer_restore_count;

static bool quickjs_test_buffer_is_zero(const void *object) {
  const quickjs_test_buffer *value = (const quickjs_test_buffer *)object;
  return value != NULL && value->data == NULL && value->size == 0u;
}

static cmeta_status quickjs_test_buffer_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
  quickjs_test_buffer *value = (quickjs_test_buffer *)object;
  unsigned char *copy = NULL;
  if (value == NULL || (size != 0u && data == NULL))
    return CMETA_INVALID_ARGUMENT;
  if (size > max_bytes) return CMETA_CAPACITY_EXCEEDED;
  if (size != 0u) {
    copy = (unsigned char *)malloc(size);
    if (copy == NULL) return CMETA_OUT_OF_MEMORY;
    memcpy(copy, data, size);
  }
  value->data = copy;
  value->size = size;
  return CMETA_OK;
}

static cmeta_status quickjs_test_buffer_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
  const quickjs_test_buffer *value = (const quickjs_test_buffer *)object;
  if (value == NULL || out_data == NULL || out_size == NULL)
    return CMETA_INVALID_ARGUMENT;
  *out_data = value->data;
  *out_size = value->size;
  return CMETA_OK;
}

static cmeta_status quickjs_test_buffer_init_zero(void *object) {
  quickjs_test_buffer *value = (quickjs_test_buffer *)object;
  if (value == NULL) return CMETA_INVALID_ARGUMENT;
  value->data = NULL;
  value->size = 0u;
  return CMETA_OK;
}

static void quickjs_test_buffer_restore_zero(void *object) {
  quickjs_test_buffer *value = (quickjs_test_buffer *)object;
  if (value == NULL) return;
  free(value->data);
  value->data = NULL;
  value->size = 0u;
  ++quickjs_test_buffer_restore_count;
}

static void quickjs_test_buffer_move(void *destination, void *source) {
  quickjs_test_buffer *to = (quickjs_test_buffer *)destination;
  quickjs_test_buffer *from = (quickjs_test_buffer *)source;
  if (to == NULL || from == NULL) return;
  *to = *from;
  from->data = NULL;
  from->size = 0u;
}

static const cmeta_type_identity quickjs_test_buffer_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.quickjs.Buffer");
static const cmeta_type_desc quickjs_test_buffer_type = {
    .name = "quickjs_test_buffer",
    .size = sizeof(quickjs_test_buffer),
    .align = _Alignof(quickjs_test_buffer),
    .kind = CMETA_T_OBJECT,
    .identity = &quickjs_test_buffer_identity
};
static const cmeta_data_buffer_shape quickjs_test_buffer_shape = {
    .ownership = CMETA_DATA_BUFFER_OWNED
};
static const cmeta_data_buffer_ops quickjs_test_buffer_ops = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &quickjs_test_buffer_type,
    .ownership = CMETA_DATA_BUFFER_OWNED,
    .is_zero = quickjs_test_buffer_is_zero,
    .assign = quickjs_test_buffer_assign,
    .restore_zero = quickjs_test_buffer_restore_zero,
    .read = quickjs_test_buffer_read,
    .init_zero = quickjs_test_buffer_init_zero,
    .move = quickjs_test_buffer_move
};
static const cmeta_data_desc quickjs_test_string_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.quickjs.String.data",
    .display_name = "QuickJS test string",
    .kind = CMETA_DATA_STRING,
    .storage_type = &quickjs_test_buffer_type,
    .shape = &quickjs_test_buffer_shape,
    .buffer_ops = &quickjs_test_buffer_ops
};
static const cmeta_data_desc quickjs_test_bytes_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.quickjs.Bytes.data",
    .display_name = "QuickJS test bytes",
    .kind = CMETA_DATA_BYTES,
    .storage_type = &quickjs_test_buffer_type,
    .shape = &quickjs_test_buffer_shape,
    .buffer_ops = &quickjs_test_buffer_ops
};

Enum(quickjs_test_state,
    (QUICKJS_TEST_IDLE, 1, "idle"),
    (QUICKJS_TEST_READY, 2, "ready")
);

static const cmeta_type_identity quickjs_test_state_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.quickjs.State");
static const cmeta_type_desc quickjs_test_state_type = {
    .name = "quickjs_test_state",
    .size = sizeof(quickjs_test_state),
    .align = _Alignof(quickjs_test_state),
    .kind = CMETA_T_INTEGER,
    .identity = &quickjs_test_state_identity
};
static bool quickjs_test_state_is_zero(const void *object) {
  return object != NULL &&
         CMETA_ENUM_TO_INT64(*(const quickjs_test_state *)object) == 0;
}
static cmeta_status quickjs_test_state_read(
    const void *object, int64_t *out) {
  if (object == NULL || out == NULL) return CMETA_INVALID_ARGUMENT;
  *out = CMETA_ENUM_TO_INT64(*(const quickjs_test_state *)object);
  return CMETA_OK;
}
static cmeta_status quickjs_test_state_assign(void *object, int64_t value) {
  if (object == NULL) return CMETA_INVALID_ARGUMENT;
  *(quickjs_test_state *)object =
      CMETA_ENUM_FROM_INT64(quickjs_test_state, value);
  return CMETA_OK;
}
static void quickjs_test_state_restore(void *object) {
  if (object != NULL)
    *(quickjs_test_state *)object =
        CMETA_ENUM_FROM_INT64(quickjs_test_state, 0);
}
static const cmeta_data_enum_shape quickjs_test_state_shape = {
    .meta = EnumMeta(quickjs_test_state)
};
static const cmeta_data_enum_ops quickjs_test_state_ops = {
    .struct_size = sizeof(cmeta_data_enum_ops),
    .abi_version = CMETA_DATA_ENUM_OPS_ABI_VERSION,
    .storage_type = &quickjs_test_state_type,
    .is_zero = quickjs_test_state_is_zero,
    .read = quickjs_test_state_read,
    .assign = quickjs_test_state_assign,
    .restore_zero = quickjs_test_state_restore
};
static const cmeta_data_desc quickjs_test_state_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.quickjs.State.data",
    .display_name = "QuickJS test state",
    .kind = CMETA_DATA_ENUM,
    .storage_type = &quickjs_test_state_type,
    .shape = &quickjs_test_state_shape,
    .enum_ops = &quickjs_test_state_ops
};

typedef struct quickjs_test_record {
  quickjs_test_buffer label;
  int id;
} quickjs_test_record;

static const cmeta_type_identity quickjs_test_record_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.quickjs.Record");
static const cmeta_type_desc quickjs_test_record_type = {
    .name = "quickjs_test_record",
    .size = sizeof(quickjs_test_record),
    .align = _Alignof(quickjs_test_record),
    .kind = CMETA_T_OBJECT,
    .identity = &quickjs_test_record_identity
};
static const cmeta_field_desc quickjs_test_record_layout_fields[] = {
    {"label", "quickjs_test_buffer", offsetof(quickjs_test_record, label),
     sizeof(quickjs_test_buffer), _Alignof(quickjs_test_buffer),
     &quickjs_test_buffer_type, NULL},
    {"id", "int", offsetof(quickjs_test_record, id),
     sizeof(int), _Alignof(int), &cmeta_type_int, NULL}
};
static const cmeta_struct_desc quickjs_test_record_layout = {
    "quickjs_test_record", sizeof(quickjs_test_record),
    _Alignof(quickjs_test_record), quickjs_test_record_layout_fields, 2u
};
static const cmeta_data_field_desc quickjs_test_record_fields[] = {
    {"test.quickjs.Record.label", "label",
     offsetof(quickjs_test_record, label), &quickjs_test_string_data},
    {"test.quickjs.Record.id", "id",
     offsetof(quickjs_test_record, id), &cmeta_data_int}
};
static const cmeta_data_struct_shape quickjs_test_record_shape = {
    &quickjs_test_record_layout, quickjs_test_record_fields, 2u
};
static const cmeta_data_desc quickjs_test_record_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.quickjs.Record.data",
    .display_name = "QuickJS test record",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &quickjs_test_record_type,
    .shape = &quickjs_test_record_shape
};

typed(Vec, quickjs_test_vec, int);
typed(Map, quickjs_test_map, int, int);

typedef struct quickjs_test_map_capture {
  int keys[2];
  int values[2];
  size_t count;
} quickjs_test_map_capture;

static cmeta_status quickjs_test_capture_map(
    void *opaque, const void *key, const void *value) {
  quickjs_test_map_capture *capture =
      (quickjs_test_map_capture *)opaque;
  if (capture == NULL || key == NULL || value == NULL ||
      capture->count >= 2u)
    return CMETA_INVALID_ARGUMENT;
  capture->keys[capture->count] = *(const int *)key;
  capture->values[capture->count] = *(const int *)value;
  ++capture->count;
  return CMETA_OK;
}

typed_any(value, int, quickjs_test_increment, (int value)) {
  return value + 1;
}

static const cmeta_param_desc quickjs_test_increment_params[] = {
    {sizeof(cmeta_param_desc), "value", &cmeta_type_int, CMETA_PARAM_IN}
};
static const cmeta_function_desc quickjs_test_increment_function = {
    sizeof(cmeta_function_desc), "increment", &cmeta_type_int,
    quickjs_test_increment_params, 1u,
    CMETA_CONTRACT_EFFECTS(value), CMETA_CONTRACT_PROPERTIES(value)
};
static const cmeta_data_desc *const quickjs_test_increment_data_params[] = {
    &cmeta_data_int
};
static const cmeta_function_data_desc quickjs_test_increment_data = {
    sizeof(cmeta_function_data_desc), &quickjs_test_increment_function,
    &cmeta_data_int, quickjs_test_increment_data_params, 1u
};
static const cmeta_abi_carrier quickjs_test_increment_abi_params[] = {
    CMETA_ABI_SCALAR
};
static const cmeta_function_abi_desc quickjs_test_increment_abi = {
    sizeof(cmeta_function_abi_desc), &quickjs_test_increment_function,
    CMETA_ABI_SCALAR, quickjs_test_increment_abi_params, 1u
};
static const cmeta_interface_method_desc quickjs_test_increment_method = {
    sizeof(cmeta_interface_method_desc), "increment", 1u,
    CMETA_INTERFACE_METHOD_NONE, &quickjs_test_increment_function,
    &quickjs_test_increment_abi
};

static JSValue quickjs_test_eval(JSContext *context, const char *source) {
  return JS_Eval(
      context, source, strlen(source), "quickjs_cmeta_test.js",
      JS_EVAL_TYPE_GLOBAL);
}

spec("Salts QuickJS canonical CMeta binding") {
  it("projects exact scalars, enum, string and bytes in both directions") {
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *context = runtime != NULL ? JS_NewContext(runtime) : NULL;
    salts_quickjs_limits limits = {8u, 16u, 4096u};
    int32_t integer = 42;
    int32_t integer_out = 0;
    uint64_t wide = UINT64_MAX;
    uint64_t wide_out = 0u;
    quickjs_test_state state = QUICKJS_TEST_READY;
    quickjs_test_state state_out =
        CMETA_ENUM_FROM_INT64(quickjs_test_state, 0);
    quickjs_test_buffer string_value = {0};
    quickjs_test_buffer string_out = {0};
    quickjs_test_buffer bytes_value = {0};
    quickjs_test_buffer bytes_out = {0};
    static const unsigned char text[] = {'s', 'a', 'l', 't'};
    static const unsigned char bytes[] = {0u, 1u, 255u};
    JSValue value = JS_UNDEFINED;
    const char *js_text;
    uint8_t *js_bytes;
    size_t size = 0u;

    check_not_null(runtime);
    check_not_null(context);
    check_equal(salts_quickjs_push_cmeta(
                    context, &cmeta_data_int32, &integer, limits, &value),
                CMETA_OK);
    check_equal(salts_quickjs_read_cmeta(
                    context, value, &cmeta_data_int32,
                    &integer_out, limits), CMETA_OK);
    check_equal(integer_out, integer);
    JS_FreeValue(context, value);

    value = JS_UNDEFINED;
    check_equal(salts_quickjs_push_cmeta(
                    context, &cmeta_data_uint64, &wide, limits, &value),
                CMETA_OK);
    check_true(JS_IsBigInt(context, value));
    check_equal(salts_quickjs_read_cmeta(
                    context, value, &cmeta_data_uint64,
                    &wide_out, limits), CMETA_OK);
    check_equal(wide_out, wide);
    JS_FreeValue(context, value);

    value = JS_UNDEFINED;
    check_equal(salts_quickjs_push_cmeta(
                    context, &quickjs_test_state_data,
                    &state, limits, &value), CMETA_OK);
    js_text = JS_ToCString(context, value);
    check_not_null(js_text);
    check_equal(js_text, "QUICKJS_TEST_READY");
    JS_FreeCString(context, js_text);
    check_equal(salts_quickjs_read_cmeta(
                    context, value, &quickjs_test_state_data,
                    &state_out, limits), CMETA_OK);
    check_equal(CMETA_ENUM_TO_INT64(state_out),
                CMETA_ENUM_TO_INT64(QUICKJS_TEST_READY));
    JS_FreeValue(context, value);

    check_equal(cmeta_data_value_init_zero(
                    &quickjs_test_string_data, &string_value), CMETA_OK);
    check_equal(cmeta_data_buffer_assign(
                    &quickjs_test_string_data, &string_value,
                    text, sizeof(text), limits.max_bytes), CMETA_OK);
    value = JS_UNDEFINED;
    check_equal(salts_quickjs_push_cmeta(
                    context, &quickjs_test_string_data,
                    &string_value, limits, &value), CMETA_OK);
    check_equal(cmeta_data_value_restore_zero(
                    &quickjs_test_string_data, &string_value), CMETA_OK);
    js_text = JS_ToCStringLen(context, &size, value);
    check_not_null(js_text);
    check_equal(size, sizeof(text));
    check_true(memcmp(js_text, text, sizeof(text)) == 0);
    JS_FreeCString(context, js_text);
    check_equal(cmeta_data_value_init_zero(
                    &quickjs_test_string_data, &string_out), CMETA_OK);
    check_equal(salts_quickjs_read_cmeta(
                    context, value, &quickjs_test_string_data,
                    &string_out, limits), CMETA_OK);
    check_equal(string_out.size, sizeof(text));
    check_true(memcmp(string_out.data, text, sizeof(text)) == 0);
    JS_FreeValue(context, value);
    check_equal(cmeta_data_value_restore_zero(
                    &quickjs_test_string_data, &string_out), CMETA_OK);

    check_equal(cmeta_data_value_init_zero(
                    &quickjs_test_bytes_data, &bytes_value), CMETA_OK);
    check_equal(cmeta_data_buffer_assign(
                    &quickjs_test_bytes_data, &bytes_value,
                    bytes, sizeof(bytes), limits.max_bytes), CMETA_OK);
    value = JS_UNDEFINED;
    check_equal(salts_quickjs_push_cmeta(
                    context, &quickjs_test_bytes_data,
                    &bytes_value, limits, &value), CMETA_OK);
    check_true(JS_IsArrayBuffer(value));
    check_equal(cmeta_data_value_restore_zero(
                    &quickjs_test_bytes_data, &bytes_value), CMETA_OK);
    js_bytes = JS_GetArrayBuffer(context, &size, value);
    check_not_null(js_bytes);
    check_equal(size, sizeof(bytes));
    check_true(memcmp(js_bytes, bytes, sizeof(bytes)) == 0);
    check_equal(cmeta_data_value_init_zero(
                    &quickjs_test_bytes_data, &bytes_out), CMETA_OK);
    check_equal(salts_quickjs_read_cmeta(
                    context, value, &quickjs_test_bytes_data,
                    &bytes_out, limits), CMETA_OK);
    check_equal(bytes_out.size, sizeof(bytes));
    check_true(memcmp(bytes_out.data, bytes, sizeof(bytes)) == 0);
    JS_FreeValue(context, value);
    check_equal(cmeta_data_value_restore_zero(
                    &quickjs_test_bytes_data, &bytes_out), CMETA_OK);

    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
  }

  it("projects struct properties transactionally and enforces limits") {
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *context = runtime != NULL ? JS_NewContext(runtime) : NULL;
    salts_quickjs_limits limits = {8u, 8u, 4096u};
    salts_quickjs_limits shallow = {0u, 8u, 4096u};
    salts_quickjs_limits short_bytes = {8u, 8u, 2u};
    quickjs_test_record source = {0};
    quickjs_test_record destination = {0};
    quickjs_test_record failed = {0};
    static const unsigned char label[] = {'o', 'w', 'n', 'e', 'd'};
    JSValue value = JS_UNDEFINED;
    JSValue bad = JS_UNDEFINED;
    JSValue property = JS_UNDEFINED;
    int32_t id = 0;
    size_t restores_before;

    check_not_null(context);
    check_equal(cmeta_data_value_init_zero(
                    &quickjs_test_record_data, &source), CMETA_OK);
    check_equal(cmeta_data_buffer_assign(
                    &quickjs_test_string_data, &source.label,
                    label, sizeof(label), limits.max_bytes), CMETA_OK);
    source.id = 7;
    check_equal(salts_quickjs_push_cmeta(
                    context, &quickjs_test_record_data,
                    &source, limits, &value), CMETA_OK);
    check_equal(cmeta_data_value_restore_zero(
                    &quickjs_test_record_data, &source), CMETA_OK);
    property = JS_GetPropertyStr(context, value, "id");
    check_equal(JS_ToInt32(context, &id, property), 0);
    check_equal(id, 7);
    JS_FreeValue(context, property);

    check_equal(cmeta_data_value_init_zero(
                    &quickjs_test_record_data, &destination), CMETA_OK);
    check_equal(salts_quickjs_read_cmeta(
                    context, value, &quickjs_test_record_data,
                    &destination, limits), CMETA_OK);
    check_equal(destination.id, 7);
    check_equal(destination.label.size, sizeof(label));
    check_true(memcmp(destination.label.data, label, sizeof(label)) == 0);
    check_equal(cmeta_data_value_restore_zero(
                    &quickjs_test_record_data, &destination), CMETA_OK);

    check_equal(salts_quickjs_push_cmeta(
                    context, &quickjs_test_record_data,
                    &source, shallow, &property),
                CMETA_CAPACITY_EXCEEDED);
    check_true(JS_IsUndefined(property));

    bad = quickjs_test_eval(context, "({label:'owned', id:'bad'})");
    check_false(JS_IsException(bad));
    check_equal(cmeta_data_value_init_zero(
                    &quickjs_test_record_data, &failed), CMETA_OK);
    restores_before = quickjs_test_buffer_restore_count;
    check_equal(salts_quickjs_read_cmeta(
                    context, bad, &quickjs_test_record_data,
                    &failed, limits), CMETA_TYPE_MISMATCH);
    check_true(quickjs_test_buffer_is_zero(&failed.label));
    check_equal(failed.id, 0);
    check_true(quickjs_test_buffer_restore_count > restores_before);
    check_equal(salts_quickjs_read_cmeta(
                    context, bad, &quickjs_test_record_data,
                    &failed, short_bytes), CMETA_CAPACITY_EXCEEDED);
    check_true(quickjs_test_buffer_is_zero(&failed.label));
    check_equal(failed.id, 0);
    check_equal(salts_quickjs_read_cmeta(
                    context, bad, &quickjs_test_record_data,
                    &failed, shallow), CMETA_CAPACITY_EXCEEDED);
    check_true(quickjs_test_buffer_is_zero(&failed.label));
    check_equal(failed.id, 0);

    JS_FreeValue(context, bad);
    JS_FreeValue(context, value);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
  }

  it("round trips provider-neutral collections and maps") {
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *context = runtime != NULL ? JS_NewContext(runtime) : NULL;
    salts_quickjs_limits limits = {8u, 8u, 4096u};
    salts_quickjs_limits one_item = {8u, 1u, 4096u};
    quickjs_test_vec source_vec = {0};
    quickjs_test_vec output_vec = {0};
    quickjs_test_vec limited_vec = {0};
    quickjs_test_map source_map = {0};
    quickjs_test_map output_map = {0};
    quickjs_test_map_capture capture = {{0}, {0}, 0u};
    JSValue value = JS_UNDEFINED;
    JSValue input = JS_UNDEFINED;
    int64_t length = 0;

    check_not_null(context);
    check_equal(quickjs_test_vec_init(&source_vec, 8u), STL_OK);
    check_equal(quickjs_test_vec_push(&source_vec, 3), STL_OK);
    check_equal(quickjs_test_vec_push(&source_vec, 5), STL_OK);
    check_equal(salts_quickjs_push_cmeta(
                    context, &quickjs_test_vec_collection_data,
                    &source_vec, limits, &value), CMETA_OK);
    check_true(JS_IsArray(value));
    check_equal(JS_GetLength(context, value, &length), 0);
    check_equal(length, 2);
    quickjs_test_vec_destroy(&source_vec);
    JS_FreeValue(context, value);

    input = quickjs_test_eval(context, "[4, 6]");
    check_false(JS_IsException(input));
    check_equal(cmeta_data_construct_init_zero(
                    &quickjs_test_vec_collection_data, &output_vec), CMETA_OK);
    check_equal(salts_quickjs_read_cmeta(
                    context, input, &quickjs_test_vec_collection_data,
                    &output_vec, limits), CMETA_OK);
    check_equal(quickjs_test_vec_size(&output_vec), 2u);
    check_equal(*quickjs_test_vec_at_const(&output_vec, 0u), 4);
    check_equal(*quickjs_test_vec_at_const(&output_vec, 1u), 6);
    quickjs_test_vec_destroy(&output_vec);
    check_equal(cmeta_data_construct_init_zero(
                    &quickjs_test_vec_collection_data, &limited_vec),
                CMETA_OK);
    check_equal(salts_quickjs_read_cmeta(
                    context, input, &quickjs_test_vec_collection_data,
                    &limited_vec, one_item), CMETA_CAPACITY_EXCEEDED);
    check_equal(quickjs_test_vec_size(&limited_vec), 0u);
    quickjs_test_vec_destroy(&limited_vec);
    JS_FreeValue(context, input);

    check_equal(quickjs_test_map_init(&source_map, 8u), STL_OK);
    check_equal(quickjs_test_map_put(&source_map, 5, 50), STL_OK);
    check_equal(quickjs_test_map_put(&source_map, 3, 30), STL_OK);
    value = JS_UNDEFINED;
    check_equal(salts_quickjs_push_cmeta(
                    context, &quickjs_test_map_map_data,
                    &source_map, limits, &value), CMETA_OK);
    check_true(JS_IsArray(value));
    quickjs_test_map_destroy(&source_map);
    JS_FreeValue(context, value);

    input = quickjs_test_eval(
        context, "[{key:3,value:30},{key:5,value:50}]");
    check_false(JS_IsException(input));
    check_equal(cmeta_data_construct_init_zero(
                    &quickjs_test_map_map_data, &output_map), CMETA_OK);
    check_equal(salts_quickjs_read_cmeta(
                    context, input, &quickjs_test_map_map_data,
                    &output_map, limits), CMETA_OK);
    check_equal(cmeta_data_map_foreach(
                    &quickjs_test_map_map_data, &output_map,
                    quickjs_test_capture_map, &capture, 2u), CMETA_OK);
    check_equal(capture.count, 2u);
    check_equal(capture.keys[0], 3);
    check_equal(capture.values[0], 30);
    check_equal(capture.keys[1], 5);
    check_equal(capture.values[1], 50);
    quickjs_test_map_destroy(&output_map);
    JS_FreeValue(context, input);

    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
  }

  it("invokes reflected functions and interface methods through FunctionData") {
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *context = runtime != NULL ? JS_NewContext(runtime) : NULL;
    salts_quickjs_limits limits = {8u, 8u, 4096u};
    cmeta_invokable invokable = CMETA_INVOKABLE_INIT;
    JSValue argument = JS_NewInt32(context, 41);
    JSValue result = JS_UNDEFINED;
    bool has_result = false;
    int32_t native_result = 0;

    check_not_null(context);
    check_equal(cmeta_interface_method_invokable_bind(
                    &quickjs_test_increment_method,
                    &quickjs_test_increment_data,
                    quickjs_test_increment, &invokable), CMETA_OK);
    check_equal(salts_quickjs_call_invokable(
                    context, &invokable, 1, &argument, limits,
                    &result, &has_result), CMETA_OK);
    check_true(has_result);
    check_equal(JS_ToInt32(context, &native_result, result), 0);
    check_equal(native_result, 42);
    JS_FreeValue(context, result);
    JS_FreeValue(context, argument);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
  }
}
