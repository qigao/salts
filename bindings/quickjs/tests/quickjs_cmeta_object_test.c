#include <salts/bindings/quickjs/cmeta.h>

#include <cmeta/invokable.h>
#include <cmeta/object.h>
#include <quickjs.h>
#include <string.h>
#include "tinytest.h"

typedef struct quickjs_object_box {
  int value;
} quickjs_object_box;

static const cmeta_type_desc quickjs_object_box_type = {
    .name = "quickjs_object_box",
    .size = sizeof(quickjs_object_box),
    .align = _Alignof(quickjs_object_box),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_type_desc quickjs_object_box_ptr_type = {
    .name = "quickjs_object_box *",
    .size = sizeof(quickjs_object_box *),
    .align = _Alignof(quickjs_object_box *),
    .kind = CMETA_T_POINTER,
    .pointee = &quickjs_object_box_type,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_field_desc quickjs_object_layout_fields[] = {
    {
        .name = "value",
        .type_name = "int",
        .offset = offsetof(quickjs_object_box, value),
        .size = sizeof(int),
        .align = _Alignof(int),
        .type = &cmeta_type_int,
        .declared_type = NULL
    }
};

static const cmeta_struct_desc quickjs_object_layout = {
    .name = "quickjs_object_box",
    .size = sizeof(quickjs_object_box),
    .align = _Alignof(quickjs_object_box),
    .fields = quickjs_object_layout_fields,
    .field_count = 1u
};

static const cmeta_data_field_desc quickjs_object_data_fields[] = {
    {
        .stable_id = "test.quickjs_object_box.value",
        .name = "value",
        .offset = offsetof(quickjs_object_box, value),
        .value = &cmeta_data_int
    }
};

static const cmeta_data_struct_shape quickjs_object_shape = {
    .layout = &quickjs_object_layout,
    .fields = quickjs_object_data_fields,
    .field_count = 1u
};

static const cmeta_data_desc quickjs_object_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.quickjs_object_box.data",
    .display_name = "quickjs_object_box",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &quickjs_object_box_type,
    .shape = &quickjs_object_shape,
    .buffer_ops = NULL,
    .enum_ops = NULL,
    .variant_ops = NULL,
    .fixed_ops = NULL,
    .enum_bits_ops = NULL,
    .collection_ops = NULL,
    .map_ops = NULL,
    .construct_ops = NULL
};

static const cmeta_param_desc quickjs_object_add_params[] = {
    {
        .size = sizeof(cmeta_param_desc),
        .name = "self",
        .type = &quickjs_object_box_ptr_type,
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

static const cmeta_function_desc quickjs_object_add_function = {
    .size = sizeof(cmeta_function_desc),
    .name = "quickjs_object_box_add",
    .return_type = &cmeta_type_int,
    .params = quickjs_object_add_params,
    .param_count = 2u,
    .effects = CMETA_EFFECT_STATEFUL,
    .properties = CMETA_PROP_NONE
};

static const cmeta_abi_carrier quickjs_object_add_param_abi[] = {
    CMETA_ABI_OBJECT_POINTER, CMETA_ABI_SCALAR
};

static const cmeta_function_abi_desc quickjs_object_add_abi = {
    .size = sizeof(cmeta_function_abi_desc),
    .function = &quickjs_object_add_function,
    .return_carrier = CMETA_ABI_SCALAR,
    .param_carriers = quickjs_object_add_param_abi,
    .param_count = 2u
};

static const cmeta_receiver_method quickjs_object_methods[] = {
    {
        .name = "add",
        .function = &quickjs_object_add_function,
        .abi = &quickjs_object_add_abi
    }
};

static const cmeta_receiver_method_set quickjs_object_method_set = {
    .size = sizeof(cmeta_receiver_method_set),
    .receiver_type = &quickjs_object_box_type,
    .methods = quickjs_object_methods,
    .method_count = 1u,
    .owner_name = "QuickJSObjectBox"
};

static const cmeta_param_desc quickjs_object_add_projected_params[] = {
    {
        .size = sizeof(cmeta_param_desc),
        .name = "delta",
        .type = &cmeta_type_int,
        .flags = CMETA_PARAM_IN
    }
};

static const cmeta_function_desc quickjs_object_add_projected_function = {
    .size = sizeof(cmeta_function_desc),
    .name = "QuickJSObjectBox.bound_add",
    .return_type = &cmeta_type_int,
    .params = quickjs_object_add_projected_params,
    .param_count = 1u,
    .effects = CMETA_EFFECT_STATEFUL,
    .properties = CMETA_PROP_NONE
};

static const cmeta_data_desc *const quickjs_object_add_projected_data_params[] = {
    &cmeta_data_int
};

static const cmeta_function_data_desc quickjs_object_add_projected_data = {
    .size = sizeof(cmeta_function_data_desc),
    .function = &quickjs_object_add_projected_function,
    .return_data = &cmeta_data_int,
    .params = quickjs_object_add_projected_data_params,
    .param_count = 1u
};

typed_any(value, int, quickjs_object_add_callable_shape, (int delta)) {
  return delta;
}

static int quickjs_object_box_add(quickjs_object_box *self, int delta) {
  self->value += delta;
  return self->value;
}

static bool quickjs_object_bound_add_invoke(
    const cmeta_callable *self, void *out, const void *const *args) {
  quickjs_object_box *receiver = NULL;
  int delta;
  int result;

  if (self == NULL || out == NULL || args == NULL || args[0] == NULL ||
      self->capture_size != sizeof(receiver))
    return false;
  memcpy(&receiver, self->capture.bytes, sizeof(receiver));
  if (receiver == NULL)
    return false;
  memcpy(&delta, args[0], sizeof(delta));
  result = quickjs_object_box_add(receiver, delta);
  memcpy(out, &result, sizeof(result));
  return true;
}

static cmeta_status quickjs_object_method_bind(
    void *context, void *object, const cmeta_receiver_method *method,
    cmeta_object_method_binding *out) {
  quickjs_object_box *receiver = (quickjs_object_box *)object;
  cmeta_callable callable = quickjs_object_add_callable_shape;

  (void)context;
  if (receiver == NULL || method != &quickjs_object_methods[0] || out == NULL)
    return CMETA_INVALID_ARGUMENT;

  *out = (cmeta_object_method_binding)CMETA_OBJECT_METHOD_BINDING_INIT;
  callable.invoke = quickjs_object_bound_add_invoke;
  callable.dispatch = CMETA_CALLABLE_DISPATCH_ADAPTER;
  callable.capture_size = sizeof(receiver);
  memcpy(callable.capture.bytes, &receiver, sizeof(receiver));
  callable.meta.effects = quickjs_object_add_projected_function.effects;
  callable.meta.properties = quickjs_object_add_projected_function.properties;

  out->data = &quickjs_object_add_projected_data;
  out->callable = callable;
  return CMETA_OK;
}

static const cmeta_object_method_provider quickjs_object_method_provider = {
    .size = sizeof(cmeta_object_method_provider),
    .methods = &quickjs_object_method_set,
    .context = NULL,
    .bind = quickjs_object_method_bind
};


static cmeta_status quickjs_object_field_assign(
    void *context, void *object, const cmeta_data_field_desc *field,
    const void *value) {
  quickjs_object_box *box = (quickjs_object_box *)object;
  (void)context;
  if (box == NULL || field == NULL || value == NULL)
    return CMETA_INVALID_ARGUMENT;
  if (field != &quickjs_object_data_fields[0])
    return CMETA_TRAIT_MISSING;
  box->value = *(const int *)value;
  return CMETA_OK;
}

static const cmeta_object_field_provider quickjs_object_field_provider = {
    .size = sizeof(cmeta_object_field_provider),
    .data = &quickjs_object_data,
    .context = NULL,
    .assign = quickjs_object_field_assign
};

typedef struct quickjs_object_lifecycle_counts {
  int destroys;
} quickjs_object_lifecycle_counts;

static void quickjs_object_destroy(void *context, void *object) {
  quickjs_object_lifecycle_counts *counts =
      (quickjs_object_lifecycle_counts *)context;
  if (counts != NULL && object != NULL)
    counts->destroys += 1;
}

static JSValue quickjs_object_eval(JSContext *context, const char *source) {
  return JS_Eval(
      context, source, strlen(source), "quickjs_object_test.js",
      JS_EVAL_TYPE_GLOBAL);
}

static bool quickjs_object_publish_global(
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

spec("Salts QuickJS canonical native object projection") {
  it("reads live fields and invokes extracted methods on the same native instance") {
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *context = runtime != NULL ? JS_NewContext(runtime) : NULL;
    quickjs_object_box box = {10};
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    salts_quickjs_limits limits = {8u, 8u, 4096u};
    JSValue proxy = JS_UNDEFINED;
    JSValue result = JS_UNDEFINED;
    int32_t number = 0;

    check_not_null(runtime);
    check_not_null(context);
    check_equal(cmeta_object_borrow_with_providers(
                    &object, &box, &quickjs_object_data,
                    &quickjs_object_field_provider,
                    &quickjs_object_method_provider),
                CMETA_OK);
    check_equal(salts_quickjs_push_object(
                    context, &object, limits, &proxy),
                CMETA_OK);
    check_false(cmeta_object_ref_valid(&object));
    check_true(quickjs_object_publish_global(context, "counter", proxy));
    proxy = JS_UNDEFINED;

    result = quickjs_object_eval(context, "counter.value");
    check_false(JS_IsException(result));
    check_equal(JS_ToInt32(context, &number, result), 0);
    check_equal(number, 10);
    JS_FreeValue(context, result);

    result = quickjs_object_eval(
        context, "globalThis.add = counter.add; add(5)");
    check_false(JS_IsException(result));
    check_equal(JS_ToInt32(context, &number, result), 0);
    check_equal(number, 15);
    check_equal(box.value, 15);
    JS_FreeValue(context, result);

    box.value = 20;
    result = quickjs_object_eval(context, "counter.value");
    check_false(JS_IsException(result));
    check_equal(JS_ToInt32(context, &number, result), 0);
    check_equal(number, 20);
    JS_FreeValue(context, result);

    result = quickjs_object_eval(
        context, "counter.value = 99; counter.value");
    check_false(JS_IsException(result));
    check_equal(JS_ToInt32(context, &number, result), 0);
    check_equal(number, 99);
    JS_FreeValue(context, result);
    check_equal(box.value, 99);

    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
  }

  it("keeps reflected fields read-only without mutation authority") {
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *context = runtime != NULL ? JS_NewContext(runtime) : NULL;
    quickjs_object_box box = {7};
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    salts_quickjs_limits limits = {8u, 8u, 4096u};
    JSValue proxy = JS_UNDEFINED;
    JSValue result = JS_UNDEFINED;

    check_not_null(runtime);
    check_not_null(context);
    check_equal(cmeta_object_borrow_with_provider(
                    &object, &box, &quickjs_object_data,
                    &quickjs_object_method_provider),
                CMETA_OK);
    check_equal(salts_quickjs_push_object(
                    context, &object, limits, &proxy),
                CMETA_OK);
    check_true(quickjs_object_publish_global(context, "counter", proxy));
    proxy = JS_UNDEFINED;

    result = quickjs_object_eval(context, "counter.value = 88");
    check_true(JS_IsException(result));
    JS_FreeValue(context, result);
    check_equal(box.value, 7);

    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
  }

  it("keeps an owned object alive through an extracted method closure") {
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *context = runtime != NULL ? JS_NewContext(runtime) : NULL;
    quickjs_object_box box = {3};
    quickjs_object_lifecycle_counts counts = {0};
    cmeta_object_lifecycle lifecycle = {
        .size = sizeof(cmeta_object_lifecycle),
        .context = &counts,
        .retain = NULL,
        .release = NULL,
        .destroy = quickjs_object_destroy
    };
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    salts_quickjs_limits limits = {8u, 8u, 4096u};
    JSValue proxy = JS_UNDEFINED;
    JSValue result = JS_UNDEFINED;

    check_not_null(runtime);
    check_not_null(context);
    check_equal(cmeta_object_borrow_with_provider(
                    &object, &box, &quickjs_object_data,
                    &quickjs_object_method_provider),
                CMETA_OK);
    check_equal(cmeta_object_take(&object, &lifecycle), CMETA_OK);
    check_equal(salts_quickjs_push_object(
                    context, &object, limits, &proxy),
                CMETA_OK);
    check_true(quickjs_object_publish_global(context, "owned_counter", proxy));
    proxy = JS_UNDEFINED;

    result = quickjs_object_eval(
        context,
        "globalThis.saved_add = owned_counter.add;"
        "globalThis.owned_counter = undefined;");
    check_false(JS_IsException(result));
    JS_FreeValue(context, result);
    JS_RunGC(runtime);
    check_equal(counts.destroys, 0);

    result = quickjs_object_eval(
        context, "globalThis.saved_add = undefined;");
    check_false(JS_IsException(result));
    JS_FreeValue(context, result);
    JS_RunGC(runtime);
    check_equal(counts.destroys, 1);

    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
    check_equal(counts.destroys, 1);
  }

  it("rejects field and method name collisions before handle transfer") {
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *context = runtime != NULL ? JS_NewContext(runtime) : NULL;
    quickjs_object_box box = {1};
    cmeta_field_desc layout_field = quickjs_object_layout_fields[0];
    cmeta_struct_desc layout = quickjs_object_layout;
    cmeta_data_field_desc data_field = quickjs_object_data_fields[0];
    cmeta_data_struct_shape shape = quickjs_object_shape;
    cmeta_data_desc data = quickjs_object_data;
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    salts_quickjs_limits limits = {8u, 8u, 4096u};
    JSValue proxy = JS_UNDEFINED;

    check_not_null(runtime);
    check_not_null(context);
    layout_field.name = "add";
    layout.fields = &layout_field;
    data_field.name = "add";
    shape.layout = &layout;
    shape.fields = &data_field;
    data.shape = &shape;
    check_true(cmeta_data_desc_valid(&data));

    check_equal(cmeta_object_borrow_with_provider(
                    &object, &box, &data,
                    &quickjs_object_method_provider),
                CMETA_OK);
    check_equal(salts_quickjs_push_object(
                    context, &object, limits, &proxy),
                CMETA_TYPE_MISMATCH);
    check_true(cmeta_object_ref_valid(&object));
    check_true(JS_IsUndefined(proxy));

    cmeta_object_release(&object);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
  }
}
