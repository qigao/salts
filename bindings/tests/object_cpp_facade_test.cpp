#include <salts/bindings/lua.hpp>
#include <salts/bindings/quickjs.hpp>

#include <cmeta/data.h>
#include <lauxlib.h>
#include <lua.h>
#include <quickjs.h>
#include "tinytest.hpp"

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>

struct cpp_object_box {
  int value;
};

static const cmeta_type_desc cpp_object_box_type = {
    "cpp_object_box", sizeof(cpp_object_box), alignof(cpp_object_box),
    CMETA_T_OBJECT, nullptr, nullptr, nullptr
};

static const cmeta_field_desc cpp_object_layout_fields[] = {
    {"value", "int", offsetof(cpp_object_box, value),
     sizeof(int), alignof(int), &cmeta_type_int, nullptr}
};

static const cmeta_struct_desc cpp_object_layout = {
    "cpp_object_box", sizeof(cpp_object_box), alignof(cpp_object_box),
    cpp_object_layout_fields, 1u
};

static const cmeta_data_field_desc cpp_object_data_fields[] = {
    {"test.cpp_object_box.value", "value",
     offsetof(cpp_object_box, value), &cmeta_data_int}
};

static const cmeta_data_struct_shape cpp_object_shape = {
    &cpp_object_layout, cpp_object_data_fields, 1u
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

static JSValue cpp_object_eval(JSContext *context, const char *source) {
  return JS_Eval(
      context, source, std::strlen(source), "object_cpp_facade_test.js",
      JS_EVAL_TYPE_GLOBAL);
}

static_assert(
    std::is_same_v<decltype(Salts::borrow(std::declval<cpp_object_box &>())),
                   Salts::Borrowed<cpp_object_box>>,
    "Salts::borrow is a typed pointer token, not a reflected wrapper");

spec("C++ native object binding facades") {
  it("binds one native object into both runtimes without copying it") {
    lua_State *lua = luaL_newstate();
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *js = runtime != nullptr ? JS_NewContext(runtime) : nullptr;
    cpp_object_box box{5};
    Salts::Lua::Context lua_context{
        lua, salts_lua_limits{8u, 8u, 4096u}};
    Salts::QuickJS::Context js_context{
        js, salts_quickjs_limits{8u, 8u, 4096u}};
    JSValue result = JS_UNDEFINED;
    int32_t number = 0;

    check_not_null(lua);
    check_not_null(runtime);
    check_not_null(js);

    check_equal(
        lua_context.bind_global(
            "counter", Salts::borrow(box), &cpp_object_data),
        CMETA_OK);
    check_equal(
        js_context.bind_global(
            "counter", Salts::borrow(box), &cpp_object_data),
        CMETA_OK);

    check_equal(luaL_dostring(lua, "return counter.value"), LUA_OK);
    check_equal(lua_tointeger(lua, -1), 5);
    lua_settop(lua, 0);

    result = cpp_object_eval(js, "counter.value");
    check_false(JS_IsException(result));
    check_equal(JS_ToInt32(js, &number, result), 0);
    check_equal(number, 5);
    JS_FreeValue(js, result);

    box.value = 19;

    check_equal(luaL_dostring(lua, "return counter.value"), LUA_OK);
    check_equal(lua_tointeger(lua, -1), 19);
    lua_settop(lua, 0);

    result = cpp_object_eval(js, "counter.value");
    check_false(JS_IsException(result));
    check_equal(JS_ToInt32(js, &number, result), 0);
    check_equal(number, 19);
    JS_FreeValue(js, result);

    lua_close(lua);
    JS_FreeContext(js);
    JS_FreeRuntime(runtime);
  }

  it("rejects scalar metadata for a typed C++ object token") {
    lua_State *lua = luaL_newstate();
    cpp_object_box box{1};
    Salts::Lua::Context context{
        lua, salts_lua_limits{8u, 8u, 4096u}};

    check_not_null(lua);
    check_equal(
        context.bind_global(
            "wrong", Salts::borrow(box), &cmeta_data_int),
        CMETA_TYPE_MISMATCH);
    lua_close(lua);
  }
}
