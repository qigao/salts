#include <salts/bindings/lua/cmeta.h>

#include <cmeta/data.h>
#include <lauxlib.h>
#include <lua.h>
#include "tinytest.h"

#include <stddef.h>

typedef struct salts_lua_test_child {
  int count;
  int value;
} salts_lua_test_child;

typedef struct salts_lua_test_outer {
  int payload;
  salts_lua_test_child child;
} salts_lua_test_outer;

static size_t salts_lua_test_buffer_restore_calls;

static bool salts_lua_test_buffer_is_zero(const void *object) {
  return object != NULL && *(const int *)object == 0;
}

static cmeta_status salts_lua_test_buffer_assign(
    void *object, const unsigned char *data, size_t size, size_t max_bytes) {
  (void)data;
  if (object == NULL) return CMETA_INVALID_ARGUMENT;
  if (size > max_bytes) return CMETA_CAPACITY_EXCEEDED;
  *(int *)object = (int)size;
  return CMETA_OK;
}

static cmeta_status salts_lua_test_buffer_read(
    const void *object, const unsigned char **out_data, size_t *out_size) {
  static const unsigned char bytes[] = {'a', 'b', 'c'};
  const int size = object != NULL ? *(const int *)object : -1;
  if (object == NULL || out_data == NULL || out_size == NULL ||
      size < 0 || (size_t)size > sizeof(bytes))
    return CMETA_INVALID_ARGUMENT;
  *out_data = size == 0 ? NULL : bytes;
  *out_size = (size_t)size;
  return CMETA_OK;
}

static cmeta_status salts_lua_test_buffer_init_zero(void *object) {
  if (object == NULL) return CMETA_INVALID_ARGUMENT;
  *(int *)object = 0;
  return CMETA_OK;
}

static void salts_lua_test_buffer_restore_zero(void *object) {
  if (object == NULL) return;
  *(int *)object = 0;
  ++salts_lua_test_buffer_restore_calls;
}

static void salts_lua_test_buffer_move(void *destination, void *source) {
  if (destination == NULL || source == NULL) return;
  *(int *)destination = *(int *)source;
  *(int *)source = 0;
}

static const cmeta_data_buffer_shape salts_lua_test_buffer_shape = {
    .ownership = CMETA_DATA_BUFFER_OWNED
};
static const cmeta_data_buffer_ops salts_lua_test_buffer_ops = {
    .struct_size = sizeof(cmeta_data_buffer_ops),
    .abi_version = CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    .storage_type = &cmeta_type_int,
    .ownership = CMETA_DATA_BUFFER_OWNED,
    .is_zero = salts_lua_test_buffer_is_zero,
    .assign = salts_lua_test_buffer_assign,
    .restore_zero = salts_lua_test_buffer_restore_zero,
    .read = salts_lua_test_buffer_read,
    .init_zero = salts_lua_test_buffer_init_zero,
    .move = salts_lua_test_buffer_move
};
static const cmeta_data_desc salts_lua_test_buffer_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.lua.Buffer.data",
    .display_name = "Lua test buffer",
    .kind = CMETA_DATA_BYTES,
    .storage_type = &cmeta_type_int,
    .shape = &salts_lua_test_buffer_shape,
    .buffer_ops = &salts_lua_test_buffer_ops
};

static const cmeta_type_identity salts_lua_test_child_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.lua.Child");
static const cmeta_type_desc salts_lua_test_child_type = {
    .name = "salts_lua_test_child",
    .size = sizeof(salts_lua_test_child),
    .align = _Alignof(salts_lua_test_child),
    .kind = CMETA_T_OBJECT,
    .identity = &salts_lua_test_child_identity
};
static const cmeta_field_desc salts_lua_test_child_layout_fields[] = {
    {"count", "int", offsetof(salts_lua_test_child, count),
     sizeof(int), _Alignof(int), &cmeta_type_int, NULL},
    {"value", "int", offsetof(salts_lua_test_child, value),
     sizeof(int), _Alignof(int), &cmeta_type_int, NULL}
};
static const cmeta_struct_desc salts_lua_test_child_layout = {
    "salts_lua_test_child", sizeof(salts_lua_test_child),
    _Alignof(salts_lua_test_child), salts_lua_test_child_layout_fields, 2u
};
static const cmeta_data_field_desc salts_lua_test_child_fields[] = {
    {"test.lua.Child.count", "count",
     offsetof(salts_lua_test_child, count), &cmeta_data_int},
    {"test.lua.Child.value", "value",
     offsetof(salts_lua_test_child, value), &cmeta_data_int}
};
static const cmeta_data_struct_shape salts_lua_test_child_shape = {
    &salts_lua_test_child_layout, salts_lua_test_child_fields, 2u
};
static const cmeta_data_desc salts_lua_test_child_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.lua.Child.data",
    .display_name = "Lua test child",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &salts_lua_test_child_type,
    .shape = &salts_lua_test_child_shape
};

static const cmeta_type_identity salts_lua_test_outer_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.lua.Outer");
static const cmeta_type_desc salts_lua_test_outer_type = {
    .name = "salts_lua_test_outer",
    .size = sizeof(salts_lua_test_outer),
    .align = _Alignof(salts_lua_test_outer),
    .kind = CMETA_T_OBJECT,
    .identity = &salts_lua_test_outer_identity
};
static const cmeta_field_desc salts_lua_test_outer_layout_fields[] = {
    {"payload", "int", offsetof(salts_lua_test_outer, payload),
     sizeof(int), _Alignof(int), &cmeta_type_int, NULL},
    {"child", "salts_lua_test_child", offsetof(salts_lua_test_outer, child),
     sizeof(salts_lua_test_child), _Alignof(salts_lua_test_child),
     &salts_lua_test_child_type, NULL}
};
static const cmeta_struct_desc salts_lua_test_outer_layout = {
    "salts_lua_test_outer", sizeof(salts_lua_test_outer),
    _Alignof(salts_lua_test_outer), salts_lua_test_outer_layout_fields, 2u
};
static const cmeta_data_field_desc salts_lua_test_outer_fields[] = {
    {"test.lua.Outer.payload", "payload",
     offsetof(salts_lua_test_outer, payload), &salts_lua_test_buffer_data},
    {"test.lua.Outer.child", "child",
     offsetof(salts_lua_test_outer, child), &salts_lua_test_child_data}
};
static const cmeta_data_struct_shape salts_lua_test_outer_shape = {
    &salts_lua_test_outer_layout, salts_lua_test_outer_fields, 2u
};
static const cmeta_data_desc salts_lua_test_outer_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.lua.Outer.data",
    .display_name = "Lua test outer",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &salts_lua_test_outer_type,
    .shape = &salts_lua_test_outer_shape
};

static int salts_lua_test_eval(lua_State *state, const char *source) {
  int status = luaL_loadstring(state, source);
  return status == LUA_OK ? lua_pcall(state, 0, 1, 0) : status;
}

spec("Salts Lua transactional CMeta conversion") {
  it("commits a fully converted nested struct only after success") {
    lua_State *state = luaL_newstate();
    salts_lua_limits limits = {8u, 8u, 4096u};
    salts_lua_test_outer destination = {0};

    check_not_null(state);
    check_equal(salts_lua_test_eval(
                    state,
                    "return {payload='abc', child={count=7, value=9}}"),
                LUA_OK);
    check_equal(salts_lua_read_cmeta(
                    state, -1, &salts_lua_test_outer_data,
                    &destination, limits),
                CMETA_OK);
    check_equal(destination.payload, 3);
    check_equal(destination.child.count, 7);
    check_equal(destination.child.value, 9);
    check_equal(cmeta_data_value_restore_zero(
                    &salts_lua_test_outer_data, &destination),
                CMETA_OK);
    lua_close(state);
  }

  it("rolls nested failure back without publishing the outer temporary") {
    lua_State *state = luaL_newstate();
    salts_lua_limits limits = {8u, 8u, 4096u};
    salts_lua_test_outer destination = {0};

    check_not_null(state);
    salts_lua_test_buffer_restore_calls = 0u;
    check_equal(salts_lua_test_eval(
                    state,
                    "return {payload='abc', child={count=7, value='bad'}}"),
                LUA_OK);
    check_equal(salts_lua_read_cmeta(
                    state, -1, &salts_lua_test_outer_data,
                    &destination, limits),
                CMETA_TYPE_MISMATCH);
    check_equal(destination.payload, 0);
    check_equal(destination.child.count, 0);
    check_equal(destination.child.value, 0);
    check_true(salts_lua_test_buffer_restore_calls > 0u);
    lua_close(state);
  }

  it("enforces byte and depth limits before outer commit") {
    lua_State *state = luaL_newstate();
    salts_lua_test_outer destination = {0};
    salts_lua_limits byte_limit = {8u, 8u, 2u};
    salts_lua_limits depth_limit = {1u, 8u, 4096u};

    check_not_null(state);
    check_equal(salts_lua_test_eval(
                    state,
                    "return {payload='abc', child={count=7, value=9}}"),
                LUA_OK);
    check_equal(salts_lua_read_cmeta(
                    state, -1, &salts_lua_test_outer_data,
                    &destination, byte_limit),
                CMETA_CAPACITY_EXCEEDED);
    check_equal(destination.payload, 0);
    check_equal(destination.child.count, 0);
    check_equal(destination.child.value, 0);

    check_equal(salts_lua_read_cmeta(
                    state, -1, &salts_lua_test_outer_data,
                    &destination, depth_limit),
                CMETA_CAPACITY_EXCEEDED);
    check_equal(destination.payload, 0);
    check_equal(destination.child.count, 0);
    check_equal(destination.child.value, 0);
    lua_close(state);
  }
}
