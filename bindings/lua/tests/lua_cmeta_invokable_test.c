#include <salts/bindings/lua/cmeta.h>

#include <cmeta/cmeta.h>
#include <cmeta/data.h>
#include <cmeta/invokable.h>
#include <lauxlib.h>
#include <lua.h>
#include "tinytest.h"

typed_any(value, int, salts_lua_test_increment, (int value)) {
  return value + 1;
}

static const cmeta_param_desc salts_lua_test_increment_params[] = {
    {
        .size = sizeof(cmeta_param_desc),
        .name = "value",
        .type = &cmeta_type_int,
        .flags = CMETA_PARAM_IN
    }
};

static const cmeta_function_desc salts_lua_test_increment_function = {
    .size = sizeof(cmeta_function_desc),
    .name = "increment",
    .return_type = &cmeta_type_int,
    .params = salts_lua_test_increment_params,
    .param_count = 1u,
    .effects = CMETA_CONTRACT_EFFECTS(value),
    .properties = CMETA_CONTRACT_PROPERTIES(value)
};

static const cmeta_data_desc *const salts_lua_test_increment_data_params[] = {
    &cmeta_data_int
};

static const cmeta_function_data_desc salts_lua_test_increment_data = {
    .size = sizeof(cmeta_function_data_desc),
    .function = &salts_lua_test_increment_function,
    .return_data = &cmeta_data_int,
    .params = salts_lua_test_increment_data_params,
    .param_count = 1u
};

spec("Salts Lua canonical CMeta invocation") {
  it("converts Lua arguments through FunctionData and invokes cmeta_callable") {
    lua_State *state = luaL_newstate();
    cmeta_invokable invokable = CMETA_INVOKABLE_INIT;
    salts_lua_limits limits = {8u, 8u, 4096u};
    int result_count = -1;

    check_not_null(state);
    check_equal(cmeta_invokable_bind_data(
                    &salts_lua_test_increment_data,
                    salts_lua_test_increment,
                    &invokable),
                CMETA_OK);

    lua_pushinteger(state, 41);
    check_equal(salts_lua_call_invokable(
                    state, &invokable, 1, 1u, limits, &result_count),
                CMETA_OK);
    check_equal(result_count, 1);
    check_equal(lua_tointeger(state, -1), 42);
    lua_close(state);
  }
}
