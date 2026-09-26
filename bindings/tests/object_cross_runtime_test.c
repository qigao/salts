#include <salts/bindings/lua/cmeta.h>
#include <salts/bindings/quickjs/cmeta.h>

#include <cmeta/invokable.h>
#include <cmeta/object.h>
#include <lauxlib.h>
#include <lua.h>
#include <quickjs.h>
#include <string.h>
#include "tinytest.h"

typedef struct cross_runtime_box {
  int value;
} cross_runtime_box;

static const cmeta_type_desc cross_runtime_box_type = {
    .name = "cross_runtime_box",
    .size = sizeof(cross_runtime_box),
    .align = _Alignof(cross_runtime_box),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_type_desc cross_runtime_box_ptr_type = {
    .name = "cross_runtime_box *",
    .size = sizeof(cross_runtime_box *),
    .align = _Alignof(cross_runtime_box *),
    .kind = CMETA_T_POINTER,
    .pointee = &cross_runtime_box_type,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_field_desc cross_runtime_layout_fields[] = {
    {
        .name = "value",
        .type_name = "int",
        .offset = offsetof(cross_runtime_box, value),
        .size = sizeof(int),
        .align = _Alignof(int),
        .type = &cmeta_type_int,
        .declared_type = NULL
    }
};

static const cmeta_struct_desc cross_runtime_layout = {
    .name = "cross_runtime_box",
    .size = sizeof(cross_runtime_box),
    .align = _Alignof(cross_runtime_box),
    .fields = cross_runtime_layout_fields,
    .field_count = 1u
};

static const cmeta_data_field_desc cross_runtime_data_fields[] = {
    {
        .stable_id = "test.cross_runtime_box.value",
        .name = "value",
        .offset = offsetof(cross_runtime_box, value),
        .value = &cmeta_data_int
    }
};

static const cmeta_data_struct_shape cross_runtime_shape = {
    .layout = &cross_runtime_layout,
    .fields = cross_runtime_data_fields,
    .field_count = 1u
};

static const cmeta_data_desc cross_runtime_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cross_runtime_box.data",
    .display_name = "cross_runtime_box",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cross_runtime_box_type,
    .shape = &cross_runtime_shape,
    .buffer_ops = NULL,
    .enum_ops = NULL,
    .variant_ops = NULL,
    .fixed_ops = NULL,
    .enum_bits_ops = NULL,
    .collection_ops = NULL,
    .map_ops = NULL,
    .construct_ops = NULL
};

static const cmeta_param_desc cross_runtime_add_params[] = {
    {
        .size = sizeof(cmeta_param_desc),
        .name = "self",
        .type = &cross_runtime_box_ptr_type,
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

static const cmeta_function_desc cross_runtime_add_function = {
    .size = sizeof(cmeta_function_desc),
    .name = "cross_runtime_box_add",
    .return_type = &cmeta_type_int,
    .params = cross_runtime_add_params,
    .param_count = 2u,
    .effects = CMETA_EFFECT_STATEFUL,
    .properties = CMETA_PROP_NONE
};

static const cmeta_abi_carrier cross_runtime_add_param_abi[] = {
    CMETA_ABI_OBJECT_POINTER, CMETA_ABI_SCALAR
};

static const cmeta_function_abi_desc cross_runtime_add_abi = {
    .size = sizeof(cmeta_function_abi_desc),
    .function = &cross_runtime_add_function,
    .return_carrier = CMETA_ABI_SCALAR,
    .param_carriers = cross_runtime_add_param_abi,
    .param_count = 2u
};

static const cmeta_receiver_method cross_runtime_methods[] = {
    {
        .name = "add",
        .function = &cross_runtime_add_function,
        .abi = &cross_runtime_add_abi
    }
};

static const cmeta_receiver_method_set cross_runtime_method_set = {
    .size = sizeof(cmeta_receiver_method_set),
    .receiver_type = &cross_runtime_box_type,
    .methods = cross_runtime_methods,
    .method_count = 1u,
    .owner_name = "CrossRuntimeBox"
};

static const cmeta_param_desc cross_runtime_projected_params[] = {
    {
        .size = sizeof(cmeta_param_desc),
        .name = "delta",
        .type = &cmeta_type_int,
        .flags = CMETA_PARAM_IN
    }
};

static const cmeta_function_desc cross_runtime_projected_function = {
    .size = sizeof(cmeta_function_desc),
    .name = "CrossRuntimeBox.bound_add",
    .return_type = &cmeta_type_int,
    .params = cross_runtime_projected_params,
    .param_count = 1u,
    .effects = CMETA_EFFECT_STATEFUL,
    .properties = CMETA_PROP_NONE
};

static const cmeta_data_desc *const cross_runtime_projected_data_params[] = {
    &cmeta_data_int
};

static const cmeta_function_data_desc cross_runtime_projected_data = {
    .size = sizeof(cmeta_function_data_desc),
    .function = &cross_runtime_projected_function,
    .return_data = &cmeta_data_int,
    .params = cross_runtime_projected_data_params,
    .param_count = 1u
};

typed_any(value, int, cross_runtime_callable_shape, (int delta)) {
  return delta;
}

static int cross_runtime_box_add(cross_runtime_box *self, int delta) {
  self->value += delta;
  return self->value;
}

static bool cross_runtime_bound_add_invoke(
    const cmeta_callable *self, void *out, const void *const *args) {
  cross_runtime_box *receiver = NULL;
  int delta;
  int result;

  if (self == NULL || out == NULL || args == NULL || args[0] == NULL ||
      self->capture_size != sizeof(receiver))
    return false;
  memcpy(&receiver, self->capture.bytes, sizeof(receiver));
  if (receiver == NULL)
    return false;
  memcpy(&delta, args[0], sizeof(delta));
  result = cross_runtime_box_add(receiver, delta);
  memcpy(out, &result, sizeof(result));
  return true;
}

static cmeta_status cross_runtime_method_bind(
    void *context, void *object, const cmeta_receiver_method *method,
    cmeta_object_method_binding *out) {
  cross_runtime_box *receiver = (cross_runtime_box *)object;
  cmeta_callable callable = cross_runtime_callable_shape;

  (void)context;
  if (receiver == NULL || method != &cross_runtime_methods[0] || out == NULL)
    return CMETA_INVALID_ARGUMENT;

  *out = (cmeta_object_method_binding)CMETA_OBJECT_METHOD_BINDING_INIT;
  callable.invoke = cross_runtime_bound_add_invoke;
  callable.dispatch = CMETA_CALLABLE_DISPATCH_ADAPTER;
  callable.capture_size = sizeof(receiver);
  memcpy(callable.capture.bytes, &receiver, sizeof(receiver));
  callable.meta.effects = cross_runtime_projected_function.effects;
  callable.meta.properties = cross_runtime_projected_function.properties;

  out->data = &cross_runtime_projected_data;
  out->callable = callable;
  return CMETA_OK;
}

static const cmeta_object_method_provider cross_runtime_method_provider = {
    .size = sizeof(cmeta_object_method_provider),
    .methods = &cross_runtime_method_set,
    .context = NULL,
    .bind = cross_runtime_method_bind
};


static cmeta_status cross_runtime_field_assign(
    void *context, void *object, const cmeta_data_field_desc *field,
    const void *value) {
  cross_runtime_box *box = (cross_runtime_box *)object;
  (void)context;
  if (box == NULL || field == NULL || value == NULL)
    return CMETA_INVALID_ARGUMENT;
  if (field != &cross_runtime_data_fields[0])
    return CMETA_TRAIT_MISSING;
  box->value = *(const int *)value;
  return CMETA_OK;
}

static const cmeta_object_field_provider cross_runtime_field_provider = {
    .size = sizeof(cmeta_object_field_provider),
    .data = &cross_runtime_data,
    .context = NULL,
    .assign = cross_runtime_field_assign
};

typedef struct cross_runtime_lifetime_counts {
  int retains;
  int releases;
} cross_runtime_lifetime_counts;

static cmeta_status cross_runtime_retain(void *context, void *object) {
  cross_runtime_lifetime_counts *counts =
      (cross_runtime_lifetime_counts *)context;
  if (counts == NULL || object == NULL)
    return CMETA_INVALID_ARGUMENT;
  ++counts->retains;
  return CMETA_OK;
}

static void cross_runtime_release(void *context, void *object) {
  cross_runtime_lifetime_counts *counts =
      (cross_runtime_lifetime_counts *)context;
  if (counts != NULL && object != NULL)
    ++counts->releases;
}

static JSValue cross_runtime_eval_js(
    JSContext *context, const char *source) {
  return JS_Eval(
      context, source, strlen(source), "object_cross_runtime_test.js",
      JS_EVAL_TYPE_GLOBAL);
}

static bool cross_runtime_publish_js(
    JSContext *context, const char *name, JSValue value) {
  JSValue global = JS_GetGlobalObject(context);
  int status;
  if (JS_IsException(global)) {
    JS_FreeValue(context, value);
    return false;
  }
  status = JS_SetPropertyStr(context, global, name, value);
  JS_FreeValue(context, global);
  return status >= 0;
}

spec("CMeta native object cross-runtime identity") {
  it("shares one native instance between Lua and QuickJS without translation") {
    lua_State *lua = luaL_newstate();
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *js = runtime != NULL ? JS_NewContext(runtime) : NULL;
    cross_runtime_box box = {0};
    cross_runtime_lifetime_counts counts = {0};
    cmeta_object_lifecycle lifecycle = {
        .size = sizeof(cmeta_object_lifecycle),
        .context = &counts,
        .retain = cross_runtime_retain,
        .release = cross_runtime_release,
        .destroy = NULL
    };
    cmeta_object_ref lua_object = CMETA_OBJECT_REF_INIT;
    cmeta_object_ref js_object = CMETA_OBJECT_REF_INIT;
    salts_lua_limits lua_limits = {8u, 8u, 4096u};
    salts_quickjs_limits js_limits = {8u, 8u, 4096u};
    JSValue js_proxy = JS_UNDEFINED;
    JSValue result = JS_UNDEFINED;
    int32_t js_number = 0;

    check_not_null(lua);
    check_not_null(runtime);
    check_not_null(js);

    check_equal(cmeta_object_borrow_with_providers(
                    &lua_object, &box, &cross_runtime_data,
                    &cross_runtime_field_provider,
                    &cross_runtime_method_provider),
                CMETA_OK);
    check_equal(cmeta_object_borrow_with_providers(
                    &js_object, &box, &cross_runtime_data,
                    &cross_runtime_field_provider,
                    &cross_runtime_method_provider),
                CMETA_OK);
    check_equal(cmeta_object_share(&lua_object, &lifecycle), CMETA_OK);
    check_equal(cmeta_object_share(&js_object, &lifecycle), CMETA_OK);
    check_equal(counts.retains, 2);

    check_equal(salts_lua_push_object(
                    lua, &lua_object, lua_limits),
                CMETA_OK);
    lua_setglobal(lua, "counter");
    check_equal(salts_quickjs_push_object(
                    js, &js_object, js_limits, &js_proxy),
                CMETA_OK);
    check_true(cross_runtime_publish_js(js, "counter", js_proxy));
    js_proxy = JS_UNDEFINED;

    check_equal(luaL_dostring(
                    lua, "return counter:add(10), counter.value"),
                LUA_OK);
    check_equal(lua_tointeger(lua, -2), 10);
    check_equal(lua_tointeger(lua, -1), 10);
    check_equal(box.value, 10);
    lua_settop(lua, 0);

    result = cross_runtime_eval_js(js, "counter.value");
    check_false(JS_IsException(result));
    check_equal(JS_ToInt32(js, &js_number, result), 0);
    check_equal(js_number, 10);
    JS_FreeValue(js, result);

    check_equal(luaL_dostring(
                    lua, "counter.value = 12; return counter.value"),
                LUA_OK);
    check_equal(lua_tointeger(lua, -1), 12);
    check_equal(box.value, 12);
    lua_settop(lua, 0);
    result = cross_runtime_eval_js(js, "counter.value");
    check_false(JS_IsException(result));
    check_equal(JS_ToInt32(js, &js_number, result), 0);
    check_equal(js_number, 12);
    JS_FreeValue(js, result);

    result = cross_runtime_eval_js(js, "counter.add(5)");
    check_false(JS_IsException(result));
    check_equal(JS_ToInt32(js, &js_number, result), 0);
    check_equal(js_number, 17);
    check_equal(box.value, 17);
    JS_FreeValue(js, result);

    check_equal(luaL_dostring(lua, "return counter.value"), LUA_OK);
    check_equal(lua_tointeger(lua, -1), 17);
    lua_settop(lua, 0);

    result = cross_runtime_eval_js(
        js, "counter.value = 30; counter.value");
    check_false(JS_IsException(result));
    check_equal(JS_ToInt32(js, &js_number, result), 0);
    check_equal(js_number, 30);
    check_equal(box.value, 30);
    JS_FreeValue(js, result);
    check_equal(luaL_dostring(lua, "return counter.value"), LUA_OK);
    check_equal(lua_tointeger(lua, -1), 30);
    lua_settop(lua, 0);

    check_equal(cross_runtime_box_add(&box, 2), 32);
    check_equal(luaL_dostring(lua, "return counter.value"), LUA_OK);
    check_equal(lua_tointeger(lua, -1), 32);
    lua_settop(lua, 0);
    result = cross_runtime_eval_js(js, "counter.value");
    check_false(JS_IsException(result));
    check_equal(JS_ToInt32(js, &js_number, result), 0);
    check_equal(js_number, 32);
    JS_FreeValue(js, result);

    lua_close(lua);
    lua = NULL;
    check_equal(counts.releases, 1);

    result = cross_runtime_eval_js(js, "counter.add(1)");
    check_false(JS_IsException(result));
    check_equal(JS_ToInt32(js, &js_number, result), 0);
    check_equal(js_number, 33);
    check_equal(box.value, 33);
    JS_FreeValue(js, result);

    JS_FreeContext(js);
    JS_FreeRuntime(runtime);
    check_equal(counts.releases, 2);
    check_equal(counts.retains, counts.releases);
  }
}
