#include <salts/bindings/lua/cmeta.h>

#include <cmeta/invokable.h>
#include <cmeta/object.h>
#include <lauxlib.h>
#include <lua.h>
#include <string.h>
#include "tinytest.h"

typedef struct lua_object_box {
  int value;
} lua_object_box;

static const cmeta_type_desc lua_object_box_type = {
    .name = "lua_object_box",
    .size = sizeof(lua_object_box),
    .align = _Alignof(lua_object_box),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_type_desc lua_object_box_ptr_type = {
    .name = "lua_object_box *",
    .size = sizeof(lua_object_box *),
    .align = _Alignof(lua_object_box *),
    .kind = CMETA_T_POINTER,
    .pointee = &lua_object_box_type,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_field_desc lua_object_layout_fields[] = {
    {
        .name = "value",
        .type_name = "int",
        .offset = offsetof(lua_object_box, value),
        .size = sizeof(int),
        .align = _Alignof(int),
        .type = &cmeta_type_int,
        .declared_type = NULL
    }
};

static const cmeta_struct_desc lua_object_layout = {
    .name = "lua_object_box",
    .size = sizeof(lua_object_box),
    .align = _Alignof(lua_object_box),
    .fields = lua_object_layout_fields,
    .field_count = 1u
};

static const cmeta_data_field_desc lua_object_data_fields[] = {
    {
        .stable_id = "test.lua_object_box.value",
        .name = "value",
        .offset = offsetof(lua_object_box, value),
        .value = &cmeta_data_int
    }
};

static const cmeta_data_struct_shape lua_object_shape = {
    .layout = &lua_object_layout,
    .fields = lua_object_data_fields,
    .field_count = 1u
};

static const cmeta_data_desc lua_object_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.lua_object_box.data",
    .display_name = "lua_object_box",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &lua_object_box_type,
    .shape = &lua_object_shape,
    .buffer_ops = NULL,
    .enum_ops = NULL,
    .variant_ops = NULL,
    .fixed_ops = NULL,
    .enum_bits_ops = NULL,
    .collection_ops = NULL,
    .map_ops = NULL,
    .construct_ops = NULL
};

static const cmeta_param_desc lua_object_add_params[] = {
    {
        .size = sizeof(cmeta_param_desc),
        .name = "self",
        .type = &lua_object_box_ptr_type,
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
    .name = "lua_object_box_add",
    .return_type = &cmeta_type_int,
    .params = lua_object_add_params,
    .param_count = 2u,
    .effects = CMETA_EFFECT_STATEFUL,
    .properties = CMETA_PROP_NONE
};

static const cmeta_abi_carrier lua_object_add_param_abi[] = {
    CMETA_ABI_OBJECT_POINTER, CMETA_ABI_SCALAR
};

static const cmeta_function_abi_desc lua_object_add_abi = {
    .size = sizeof(cmeta_function_abi_desc),
    .function = &lua_object_add_function,
    .return_carrier = CMETA_ABI_SCALAR,
    .param_carriers = lua_object_add_param_abi,
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
    .receiver_type = &lua_object_box_type,
    .methods = lua_object_methods,
    .method_count = 1u,
    .owner_name = "LuaObjectBox"
};

static const cmeta_param_desc lua_object_add_projected_params[] = {
    {
        .size = sizeof(cmeta_param_desc),
        .name = "delta",
        .type = &cmeta_type_int,
        .flags = CMETA_PARAM_IN
    }
};

static const cmeta_function_desc lua_object_add_projected_function = {
    .size = sizeof(cmeta_function_desc),
    .name = "LuaObjectBox.bound_add",
    .return_type = &cmeta_type_int,
    .params = lua_object_add_projected_params,
    .param_count = 1u,
    .effects = CMETA_EFFECT_STATEFUL,
    .properties = CMETA_PROP_NONE
};

static const cmeta_data_desc *const lua_object_add_projected_params_data[] = {
    &cmeta_data_int
};

static const cmeta_function_data_desc lua_object_add_projected_data = {
    .size = sizeof(cmeta_function_data_desc),
    .function = &lua_object_add_projected_function,
    .return_data = &cmeta_data_int,
    .params = lua_object_add_projected_params_data,
    .param_count = 1u
};

typed_any(value, int, lua_object_add_callable_shape, (int delta)) {
  return delta;
}

static int lua_object_box_add(lua_object_box *self, int delta) {
  self->value += delta;
  return self->value;
}

static bool lua_object_bound_add_invoke(
    const cmeta_callable *self, void *out, const void *const *args) {
  lua_object_box *receiver = NULL;
  int delta;
  int result;

  if (self == NULL || out == NULL || args == NULL || args[0] == NULL ||
      self->capture_size != sizeof(receiver))
    return false;
  memcpy(&receiver, self->capture.bytes, sizeof(receiver));
  if (receiver == NULL)
    return false;
  memcpy(&delta, args[0], sizeof(delta));
  result = lua_object_box_add(receiver, delta);
  memcpy(out, &result, sizeof(result));
  return true;
}

static cmeta_status lua_object_method_bind(
    void *context, void *object, const cmeta_receiver_method *method,
    cmeta_object_method_binding *out) {
  lua_object_box *receiver = (lua_object_box *)object;
  cmeta_callable callable = lua_object_add_callable_shape;

  (void)context;
  if (receiver == NULL || method != &lua_object_methods[0] || out == NULL)
    return CMETA_INVALID_ARGUMENT;

  *out = (cmeta_object_method_binding)CMETA_OBJECT_METHOD_BINDING_INIT;
  callable.invoke = lua_object_bound_add_invoke;
  callable.dispatch = CMETA_CALLABLE_DISPATCH_ADAPTER;
  callable.capture_size = sizeof(receiver);
  memcpy(callable.capture.bytes, &receiver, sizeof(receiver));
  callable.meta.effects = lua_object_add_projected_function.effects;
  callable.meta.properties = lua_object_add_projected_function.properties;

  out->data = &lua_object_add_projected_data;
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
  int destroys;
} lua_object_lifecycle_counts;

static void lua_object_destroy(void *context, void *object) {
  lua_object_lifecycle_counts *counts =
      (lua_object_lifecycle_counts *)context;
  if (counts != NULL && object != NULL)
    counts->destroys += 1;
}

spec("Salts Lua canonical native object projection") {
  it("projects fields and receiver methods over the same native instance") {
    lua_State *state = luaL_newstate();
    lua_object_box box = {10};
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    salts_lua_limits limits = {8u, 8u, 4096u};

    check_not_null(state);
    check_equal(cmeta_object_borrow_with_provider(
                    &object, &box, &lua_object_data,
                    &lua_object_method_provider),
                CMETA_OK);
    check_equal(salts_lua_push_object(state, &object, limits), CMETA_OK);
    check_false(cmeta_object_ref_valid(&object));
    lua_setglobal(state, "counter");

    check_equal(luaL_dostring(
                    state,
                    "return counter.value, counter:add(5), "
                    "counter.add(2), counter.value"),
                LUA_OK);
    check_equal(lua_tointeger(state, -4), 10);
    check_equal(lua_tointeger(state, -3), 15);
    check_equal(lua_tointeger(state, -2), 17);
    check_equal(lua_tointeger(state, -1), 17);
    check_equal(box.value, 17);
    lua_settop(state, 0);

    check_true(luaL_dostring(state, "counter.value = 99") != LUA_OK);
    lua_pop(state, 1);
    check_equal(box.value, 17);

    lua_close(state);
    check_equal(box.value, 17);
  }

  it("releases owned object lifetime exactly once through userdata GC") {
    lua_State *state = luaL_newstate();
    lua_object_box box = {3};
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
                    &object, &box, &lua_object_data, NULL),
                CMETA_OK);
    check_equal(cmeta_object_take(&object, &lifecycle), CMETA_OK);
    check_equal(salts_lua_push_object(state, &object, limits), CMETA_OK);
    lua_setglobal(state, "owned_counter");
    check_equal(counts.destroys, 0);

    lua_pushnil(state);
    lua_setglobal(state, "owned_counter");
    lua_gc(state, LUA_GCCOLLECT, 0);
    check_equal(counts.destroys, 1);

    lua_close(state);
    check_equal(counts.destroys, 1);
  }

  it("rejects ambiguous field and method names before moving the handle") {
    lua_State *state = luaL_newstate();
    lua_object_box box = {1};
    cmeta_field_desc layout_field = lua_object_layout_fields[0];
    cmeta_struct_desc layout = lua_object_layout;
    cmeta_data_field_desc data_field = lua_object_data_fields[0];
    cmeta_data_struct_shape shape = lua_object_shape;
    cmeta_data_desc data = lua_object_data;
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    salts_lua_limits limits = {8u, 8u, 4096u};

    check_not_null(state);
    layout_field.name = "add";
    layout.fields = &layout_field;
    data_field.name = "add";
    shape.layout = &layout;
    shape.fields = &data_field;
    data.shape = &shape;
    check_true(cmeta_data_desc_valid(&data));

    check_equal(cmeta_object_borrow_with_provider(
                    &object, &box, &data, &lua_object_method_provider),
                CMETA_OK);
    check_equal(salts_lua_push_object(state, &object, limits),
                CMETA_TYPE_MISMATCH);
    check_true(cmeta_object_ref_valid(&object));
    check_equal(lua_gettop(state), 0);

    cmeta_object_release(&object);
    lua_close(state);
  }
}
