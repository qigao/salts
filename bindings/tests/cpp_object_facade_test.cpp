#include <salts/bindings/lua.hpp>
#include <salts/bindings/quickjs.hpp>

#include <lauxlib.h>
#include <lua.h>
#include <quickjs.h>
#include "tinytest.hpp"

#include <cstddef>
#include <cstring>

struct cpp_object_box {
  int value;
};

static const cmeta_type_desc cpp_object_box_type = {
    "cpp_object_box",
    sizeof(cpp_object_box),
    alignof(cpp_object_box),
    CMETA_T_OBJECT,
    nullptr,
    nullptr,
    nullptr
};

static const cmeta_field_desc cpp_object_layout_fields[] = {
    {
        "value",
        "int",
        offsetof(cpp_object_box, value),
        sizeof(int),
        alignof(int),
        &cmeta_type_int,
        nullptr
    }
};

static const cmeta_struct_desc cpp_object_layout = {
    "cpp_object_box",
    sizeof(cpp_object_box),
    alignof(cpp_object_box),
    cpp_object_layout_fields,
    1u
};

static const cmeta_data_field_desc cpp_object_data_fields[] = {
    {
        "test.cpp_object_box.value",
        "value",
        offsetof(cpp_object_box, value),
        &cmeta_data_int
    }
};

static const cmeta_data_struct_shape cpp_object_shape = {
    &cpp_object_layout,
    cpp_object_data_fields,
    1u
};

static const cmeta_data_desc cpp_object_data = {
    sizeof(cmeta_data_desc),
    CMETA_DATA_DESC_ABI_VERSION,
    "test.cpp_object_box.data",
    "cpp_object_box",
    CMETA_DATA_STRUCT,
    &cpp_object_box_type,
    &cpp_object_shape,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr
};

static cmeta_status cpp_object_field_assign(
    void *context, void *object, const cmeta_data_field_desc *field,
    const void *value) {
  auto *box = static_cast<cpp_object_box *>(object);
  (void)context;
  if (box == nullptr || field == nullptr || value == nullptr)
    return CMETA_INVALID_ARGUMENT;
  if (field != &cpp_object_data_fields[0])
    return CMETA_TRAIT_MISSING;
  box->value = *static_cast<const int *>(value);
  return CMETA_OK;
}

static const cmeta_object_field_provider cpp_object_field_provider = {
    sizeof(cmeta_object_field_provider),
    &cpp_object_data,
    nullptr,
    cpp_object_field_assign
};

static JSValue cpp_object_eval(JSContext *context, const char *source) {
  return JS_Eval(
      context, source, std::strlen(source), "cpp_object_facade_test.js",
      JS_EVAL_TYPE_GLOBAL);
}

spec("C++ canonical object binding facade") {
  it("binds one typed native object into Lua and QuickJS without new metadata") {
    lua_State *lua_state = luaL_newstate();
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *js_context =
        runtime != nullptr ? JS_NewContext(runtime) : nullptr;
    cpp_object_box box{7};

    const salts_lua_limits lua_limits{8u, 8u, 4096u};
    const salts_quickjs_limits js_limits{8u, 8u, 4096u};

    Salts::Lua::Context lua{lua_state, lua_limits};
    Salts::QuickJS::Context js{js_context, js_limits};

    check_not_null(lua_state);
    check_not_null(runtime);
    check_not_null(js_context);

    check_equal(
        lua.bind_global(
            "counter", Salts::borrow(box), &cpp_object_data,
            &cpp_object_field_provider, nullptr),
        CMETA_OK);
    check_equal(
        js.bind_global(
            "counter", Salts::borrow(box), &cpp_object_data,
            &cpp_object_field_provider, nullptr),
        CMETA_OK);

    check_equal(
        luaL_dostring(lua_state, "counter.value = 12; return counter.value"),
        LUA_OK);
    check_equal(lua_tointeger(lua_state, -1), 12);
    lua_settop(lua_state, 0);
    check_equal(box.value, 12);

    JSValue result = cpp_object_eval(js_context, "counter.value");
    check_false(JS_IsException(result));
    int32_t number = 0;
    check_equal(JS_ToInt32(js_context, &number, result), 0);
    check_equal(number, 12);
    JS_FreeValue(js_context, result);

    result = cpp_object_eval(
        js_context, "counter.value = 19; counter.value");
    check_false(JS_IsException(result));
    check_equal(JS_ToInt32(js_context, &number, result), 0);
    check_equal(number, 19);
    JS_FreeValue(js_context, result);
    check_equal(box.value, 19);

    check_equal(luaL_dostring(lua_state, "return counter.value"), LUA_OK);
    check_equal(lua_tointeger(lua_state, -1), 19);
    lua_settop(lua_state, 0);

    lua_close(lua_state);
    JS_FreeContext(js_context);
    JS_FreeRuntime(runtime);
  }
}
