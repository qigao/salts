#include <salts/bindings/lua/cmeta.h>

#include <cmeta/cmeta.h>
#include <cmeta/data.h>
#include <cmeta/invokable.h>
#include <cmeta/object.h>
#include <lauxlib.h>
#include <lua.h>
#include <stddef.h>
#include <string.h>
#include "tinytest.h"

typedef struct lua_object_counter {
  int value;
} lua_object_counter;

static const cmeta_type_desc lua_object_counter_type = {
    .name = "lua_object_counter",
    .size = sizeof(lua_object_counter),
    .align = _Alignof(lua_object_counter),
    .kind = CMETA_T_OBJECT
};

static const cmeta_type_desc lua_object_counter_ptr_type = {
    .name = "lua_object_counter *",
    .size = sizeof(lua_object_counter *),
    .align = _Alignof(lua_object_counter *),
    .kind = CMETA_T_POINTER,
    .pointee = &lua_object_counter_type
};

static const cmeta_field_desc lua_object_counter_layout_fields[] = {
    {
        .name = "value",
        .type_name = "int",
        .offset = offsetof(lua_object_counter, value),
        .size = sizeof(int),
        .align = _Alignof(int),
        .type = &cmeta_type_int
    }
};

static const cmeta_struct_desc lua_object_counter_layout = {
    .name = "lua_object_counter",
    .size = sizeof(lua_object_counter),
    .align = _Alignof(lua_object_counter),
    .fields = lua_object_counter_layout_fields,
    .field_count = 1u
};

static const cmeta_data_field_desc lua_object_counter_fields[] = {
    {
        .stable_id = "test.lua.object.Counter.value",
        .name = "value",
        .offset = offsetof(lua_object_counter, value),
        .value = &cmeta_data_int
    }
};

static const cmeta_data_struct_shape lua_object_counter_shape = {
    .layout = &lua_object_counter_layout,
    .fields = lua_object_counter_fields,
    .field_count = 1u
};

static const cmeta_data_desc lua_object_counter_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.lua.object.Counter.data",
    .display_name = "Lua object counter",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &lua_object_counter_type,
    .shape = &lua_object_counter_shape
};

static const cmeta_param_desc lua_object_add_params[] = {
    {
        .size = sizeof(cmeta_param_desc),
        .name = "self",
        .type = &lua_object_counter_ptr_type,
        .flags = CMETA_PARAM_INOUT | CMETA_PARAM_BORROWED |
                 CMETA_PARAM_RECEIVER
    },
    {
        .size = sizeof(cmeta_param_desc),
        .name = "delta",
        .type = &cmeta_type_int,
        .flags = CMETA_PARAM_IN
    }
};

static const cmeta_function_desc lua_object_add_function = {
    .size = sizeof(cmeta_function_desc),
    .name = "lua_object_counter_add",
    .return_type = &cmeta_type_int,
    .params = lua_object_add_params,
    .param_count = 2u,
    .effects = CMETA_EFFECT_STATEFUL,
    .properties = CMETA_PROP_NONE
};

static const cmeta_abi_carrier lua_object_add_abi_params[] = {
    CMETA_ABI_OBJECT_POINTER, CMETA_ABI_SCALAR
};

static const cmeta_function_abi_desc lua_object_add_abi = {
    .size = sizeof(cmeta_function_abi_desc),
    .function = &lua_object_add_function,
    .return_carrier = CMETA_ABI_SCALAR,
    .param_carriers = lua_object_add_abi_params,
    .param_count = 2u
};

static const cmeta_receiver_method lua_object_methods[] = {
    {
        .name = "add",
        .function = &lua_object_add_function,
        .abi = &lua_object_add_abi
    }
};

static const cmeta_receiver_method_set lua_object_method_set = {
    .size = sizeof(cmeta_receiver_method_set),
    .receiver_type = &lua_object_counter_type,
    .methods = lua_object_methods,
    .method_count = 1u,
    .owner_name = "LuaCounter"
};

static const cmeta_param_desc lua_object_bound_add_params[] = {
    {
        .size = sizeof(cmeta_param_desc),
        .name = "delta",
        .type = &cmeta_type_int,
        .flags = CMETA_PARAM_IN
    }
};

static const cmeta_function_desc lua_object_bound_add_function = {
    .size = sizeof(cmeta_function_desc),
    .name = "LuaCounter.bound_add",
    .return_type = &cmeta_type_int,
    .params = lua_object_bound_add_params,
    .param_count = 1u,
    .effects = CMETA_EFFECT_STATEFUL,
    .properties = CMETA_PROP_NONE
};

static const cmeta_data_desc *const lua_object_bound_add_data_params[] = {
    &cmeta_data_int
};

static const cmeta_function_data_desc lua_object_bound_add_data = {
    .size = sizeof(cmeta_function_data_desc),
    .function = &lua_object_bound_add_function,
    .return_data = &cmeta_data_int,
    .params = lua_object_bound_add_data_params,
    .param_count = 1u
};

typed_any(value, int, lua_object_bound_add_shape, (int delta)) {
  return delta;
}

static int lua_object_counter_add(lua_object_counter *self, int delta) {
  self->value += delta;
  return self->value;
}

static bool lua_object_bound_add_invoke(
    const cmeta_callable *self, void *out, const void *const *args) {
  lua_object_counter *receiver = NULL;
  int delta;
  int result;

  if (self == NULL || out == NULL || args == NULL || args[0] == NULL ||
      self->capture_size != sizeof(receiver))
    return false;
  memcpy(&receiver, self->capture.bytes, sizeof(receiver));
  if (receiver == NULL)
    return false;
  memcpy(&delta, args[0], sizeof(delta));
  result = lua_object_counter_add(receiver, delta);
  memcpy(out, &result, sizeof(result));
  return true;
}

static cmeta_status lua_object_method_bind(
    void *context, void *object, const cmeta_receiver_method *method,
    cmeta_object_method_binding *out) {
  lua_object_counter *receiver = (lua_object_counter *)object;
  cmeta_callable callable = lua_object_bound_add_shape;

  (void)context;
  if (receiver == NULL || method != &lua_object_methods[0] || out == NULL)
    return CMETA_INVALID_ARGUMENT;

  *out = (cmeta_object_method_binding)CMETA_OBJECT_METHOD_BINDING_INIT;
  callable.invoke = lua_object_bound_add_invoke;
  callable.dispatch = CMETA_CALLABLE_DISPATCH_ADAPTER;
  callable.capture_size = sizeof(receiver);
  memcpy(callable.capture.bytes, &receiver, sizeof(receiver));
  callable.meta.effects = lua_object_bound_add_function.effects;
  callable.meta.properties = lua_object_bound_add_function.properties;

  out->data = &lua_object_bound_add_data;
  out->callable = callable;
  return CMETA_OK;
}

static const cmeta_object_method_provider lua_object_method_provider = {
    .size = sizeof(cmeta_object_method_provider),
    .methods = &lua_object_method_set,
    .context = NULL,
    .bind = lua_object_method_bind
};

typedef struct lua_object_lifecycle_counts {
  int retains;
  int releases;
  int destroys;
} lua_object_lifecycle_counts;

static cmeta_status lua_object_retain(void *context, void *object) {
  lua_object_lifecycle_counts *counts =
      (lua_object_lifecycle_counts *)context;
  if (counts == NULL || object == NULL)
    return CMETA_INVALID_ARGUMENT;
  counts->retains += 1;
  return CMETA_OK;
}

static void lua_object_release(void *context, void *object) {
  lua_object_lifecycle_counts *counts =
      (lua_object_lifecycle_counts *)context;
  if (counts != NULL && object != NULL)
    counts->releases += 1;
}

static void lua_object_destroy(void *context, void *object) {
  lua_object_lifecycle_counts *counts =
      (lua_object_lifecycle_counts *)context;
  if (counts != NULL && object != NULL)
    counts->destroys += 1;
}

static void lua_object_collect_global(lua_State *state, const char *name) {
  lua_pushnil(state);
  lua_setglobal(state, name);
  lua_gc(state, LUA_GCCOLLECT, 0);
}

spec("Salts Lua canonical native object binding") {
  it("projects fields and receiver methods over the same native instance") {
    lua_State *state = luaL_newstate();
    lua_object_counter counter = {10};
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    salts_lua_limits limits = {8u, 8u, 4096u};

    check_not_null(state);
    check_equal(cmeta_object_borrow_with_provider(
                    &object, &counter, &lua_object_counter_data,
                    &lua_object_method_provider),
                CMETA_OK);
    check_equal(salts_lua_push_object(state, &object, limits), CMETA_OK);
    check_false(cmeta_object_ref_valid(&object));
    lua_setglobal(state, "counter");

    check_equal(luaL_dostring(
                    state,
                    "return counter.value, counter:add(5), counter.value"),
                LUA_OK);
    check_equal(lua_gettop(state), 3);
    check_equal(lua_tointeger(state, -3), 10);
    check_equal(lua_tointeger(state, -2), 15);
    check_equal(lua_tointeger(state, -1), 15);
    check_equal(counter.value, 15);
    lua_settop(state, 0);

    check_equal(luaL_dostring(state, "return counter.add(2)"), LUA_OK);
    check_equal(lua_tointeger(state, -1), 17);
    check_equal(counter.value, 17);
    lua_settop(state, 0);

    check_true(luaL_dostring(state, "counter.value = 99") != LUA_OK);
    check_equal(counter.value, 17);
    lua_settop(state, 0);

    lua_close(state);
    check_equal(counter.value, 17);
  }

  it("keeps a shared object alive while a method closure retains the proxy") {
    lua_State *state = luaL_newstate();
    lua_object_counter counter = {20};
    lua_object_lifecycle_counts counts = {0};
    cmeta_object_lifecycle lifecycle = {
        .size = sizeof(cmeta_object_lifecycle),
        .context = &counts,
        .retain = lua_object_retain,
        .release = lua_object_release,
        .destroy = NULL
    };
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    salts_lua_limits limits = {8u, 8u, 4096u};

    check_not_null(state);
    check_equal(cmeta_object_borrow_with_provider(
                    &object, &counter, &lua_object_counter_data,
                    &lua_object_method_provider),
                CMETA_OK);
    check_equal(cmeta_object_share(&object, &lifecycle), CMETA_OK);
    check_equal(counts.retains, 1);
    check_equal(salts_lua_push_object(state, &object, limits), CMETA_OK);
    lua_setglobal(state, "counter");

    check_equal(luaL_dostring(
                    state, "saved_add = counter.add; counter = nil"),
                LUA_OK);
    lua_gc(state, LUA_GCCOLLECT, 0);
    check_equal(counts.releases, 0);

    check_equal(luaL_dostring(state, "return saved_add(3)"), LUA_OK);
    check_equal(lua_tointeger(state, -1), 23);
    check_equal(counter.value, 23);
    lua_settop(state, 0);

    lua_object_collect_global(state, "saved_add");
    check_equal(counts.releases, 1);
    check_equal(counts.destroys, 0);

    lua_close(state);
    check_equal(counts.releases, 1);
  }

  it("destroys an owned object once when the proxy is collected") {
    lua_State *state = luaL_newstate();
    lua_object_counter counter = {30};
    lua_object_lifecycle_counts counts = {0};
    cmeta_object_lifecycle lifecycle = {
        .size = sizeof(cmeta_object_lifecycle),
        .context = &counts,
        .retain = NULL,
        .release = NULL,
        .destroy = lua_object_destroy
    };
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    salts_lua_limits limits = {8u, 8u, 4096u};

    check_not_null(state);
    check_equal(cmeta_object_borrow(
                    &object, &counter, &lua_object_counter_data, NULL),
                CMETA_OK);
    check_equal(cmeta_object_take(&object, &lifecycle), CMETA_OK);
    check_equal(salts_lua_push_object(state, &object, limits), CMETA_OK);
    check_false(cmeta_object_ref_valid(&object));
    lua_setglobal(state, "owned");

    lua_object_collect_global(state, "owned");
    check_equal(counts.destroys, 1);
    lua_gc(state, LUA_GCCOLLECT, 0);
    check_equal(counts.destroys, 1);

    lua_close(state);
    check_equal(counts.destroys, 1);
  }
}
