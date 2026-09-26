#include <cmeta/invokable.h>
#include <cmeta/object.h>
#include <string.h>
#include "tinytest.h"

typedef struct object_box {
    int value;
} object_box;

static const cmeta_type_desc object_box_type = {
    .name = "object_box",
    .size = sizeof(object_box),
    .align = _Alignof(object_box),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_type_desc object_box_ptr_type = {
    .name = "object_box *",
    .size = sizeof(object_box *),
    .align = _Alignof(object_box *),
    .kind = CMETA_T_POINTER,
    .pointee = &object_box_type,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_field_desc object_box_layout_fields[] = {
    {
        .name = "value",
        .type_name = "int",
        .offset = offsetof(object_box, value),
        .size = sizeof(int),
        .align = _Alignof(int),
        .type = &cmeta_type_int,
        .declared_type = NULL
    }
};

static const cmeta_struct_desc object_box_layout = {
    .name = "object_box",
    .size = sizeof(object_box),
    .align = _Alignof(object_box),
    .fields = object_box_layout_fields,
    .field_count = 1u
};

static const cmeta_data_field_desc object_box_data_fields[] = {
    {
        .stable_id = "test.object_box.value",
        .name = "value",
        .offset = offsetof(object_box, value),
        .value = &cmeta_data_int
    }
};

static const cmeta_data_struct_shape object_box_shape = {
    .layout = &object_box_layout,
    .fields = object_box_data_fields,
    .field_count = 1u
};

static const cmeta_data_desc object_box_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.object_box.data",
    .display_name = "object_box",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &object_box_type,
    .shape = &object_box_shape,
    .buffer_ops = NULL,
    .enum_ops = NULL,
    .variant_ops = NULL,
    .fixed_ops = NULL,
    .enum_bits_ops = NULL,
    .collection_ops = NULL,
    .map_ops = NULL,
    .construct_ops = NULL
};

static const cmeta_param_desc object_add_params[] = {
    {
        .size = sizeof(cmeta_param_desc),
        .name = "self",
        .type = &object_box_ptr_type,
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

static const cmeta_function_desc object_add_function = {
    .size = sizeof(cmeta_function_desc),
    .name = "object_box_add",
    .return_type = &cmeta_type_int,
    .params = object_add_params,
    .param_count = 2u,
    .effects = CMETA_EFFECT_STATEFUL,
    .properties = CMETA_PROP_NONE
};

static const cmeta_abi_carrier object_add_param_abi[] = {
    CMETA_ABI_OBJECT_POINTER, CMETA_ABI_SCALAR
};

static const cmeta_function_abi_desc object_add_abi = {
    .size = sizeof(cmeta_function_abi_desc),
    .function = &object_add_function,
    .return_carrier = CMETA_ABI_SCALAR,
    .param_carriers = object_add_param_abi,
    .param_count = 2u
};

static const cmeta_receiver_method object_methods[] = {
    {
        .name = "add",
        .function = &object_add_function,
        .abi = &object_add_abi
    }
};

static const cmeta_receiver_method_set object_method_set = {
    .size = sizeof(cmeta_receiver_method_set),
    .receiver_type = &object_box_type,
    .methods = object_methods,
    .method_count = 1u,
    .owner_name = "ObjectBox"
};

static const cmeta_param_desc object_add_projected_params[] = {
    {
        .size = sizeof(cmeta_param_desc),
        .name = "delta",
        .type = &cmeta_type_int,
        .flags = CMETA_PARAM_IN
    }
};

static const cmeta_function_desc object_add_projected_function = {
    .size = sizeof(cmeta_function_desc),
    .name = "ObjectBox.bound_add",
    .return_type = &cmeta_type_int,
    .params = object_add_projected_params,
    .param_count = 1u,
    .effects = CMETA_EFFECT_STATEFUL,
    .properties = CMETA_PROP_NONE
};

static const cmeta_data_desc *const object_add_projected_data_params[] = {
    &cmeta_data_int
};

static const cmeta_function_data_desc object_add_projected_data = {
    .size = sizeof(cmeta_function_data_desc),
    .function = &object_add_projected_function,
    .return_data = &cmeta_data_int,
    .params = object_add_projected_data_params,
    .param_count = 1u
};

typed_any(value, int, object_add_callable_shape, (int delta)) {
    return delta;
}

static int object_box_add(object_box *self, int delta) {
    self->value += delta;
    return self->value;
}

static bool object_box_bound_add_invoke(
    const cmeta_callable *self, void *out, const void *const *args) {
    object_box *receiver = NULL;
    int delta;
    int result;

    if (self == NULL || out == NULL || args == NULL || args[0] == NULL ||
        self->capture_size != sizeof(receiver))
        return false;
    memcpy(&receiver, self->capture.bytes, sizeof(receiver));
    if (receiver == NULL)
        return false;
    memcpy(&delta, args[0], sizeof(delta));
    result = object_box_add(receiver, delta);
    memcpy(out, &result, sizeof(result));
    return true;
}

static cmeta_status object_box_method_bind(
    void *context, void *object, const cmeta_receiver_method *method,
    cmeta_object_method_binding *out) {
    object_box *receiver = (object_box *)object;
    cmeta_callable callable = object_add_callable_shape;

    (void)context;
    if (receiver == NULL || method != &object_methods[0] || out == NULL)
        return CMETA_INVALID_ARGUMENT;

    *out = (cmeta_object_method_binding)CMETA_OBJECT_METHOD_BINDING_INIT;
    callable.invoke = object_box_bound_add_invoke;
    callable.dispatch = CMETA_CALLABLE_DISPATCH_ADAPTER;
    callable.capture_size = sizeof(receiver);
    memcpy(callable.capture.bytes, &receiver, sizeof(receiver));
    callable.meta.effects = object_add_projected_function.effects;
    callable.meta.properties = object_add_projected_function.properties;

    out->data = &object_add_projected_data;
    out->callable = callable;
    return CMETA_OK;
}

static const cmeta_object_method_provider object_method_provider = {
    .size = sizeof(cmeta_object_method_provider),
    .methods = &object_method_set,
    .context = NULL,
    .bind = object_box_method_bind
};

spec("CMeta canonical borrowed object") {
    it("preserves one native identity without taking ownership") {
        object_box box = {7};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;

        check_equal(cmeta_object_borrow(
                        &object, &box, &object_box_data, &object_method_set),
                    CMETA_OK);
        check_true(cmeta_object_ref_valid(&object));
        check_true(object.object == &box);
        check_true(object.data == &object_box_data);
        check_true(object.methods == &object_method_set);
        check_null(object.method_provider);
        check_equal(object.lifetime, CMETA_OBJECT_LIFETIME_BORROWED);

        ((object_box *)object.object)->value = 11;
        check_equal(box.value, 11);

        check_equal(object_box_add(&box, 5), 16);
        check_equal(((object_box *)object.object)->value, 16);

        cmeta_object_release(&object);
        check_false(cmeta_object_ref_valid(&object));
        check_null(object.object);
        check_null(object.data);
        check_null(object.methods);
        check_null(object.method_provider);
        check_equal(object.lifetime, CMETA_OBJECT_LIFETIME_NONE);
        check_equal(box.value, 16);
    }

    it("reads reflected fields without copying native state") {
        object_box box = {9};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        const cmeta_data_desc *field_data = NULL;
        const void *field_value = NULL;

        check_equal(cmeta_object_borrow(
                        &object, &box, &object_box_data, &object_method_set),
                    CMETA_OK);
        check_equal(cmeta_object_field_read(
                        &object, "value", &field_data, &field_value),
                    CMETA_OK);
        check_true(field_data == &cmeta_data_int);
        check_true(field_value == &box.value);
        check_equal(*(const int *)field_value, 9);

        box.value = 12;
        check_equal(*(const int *)field_value, 12);

        field_data = &cmeta_data_long;
        field_value = &box;
        check_equal(cmeta_object_field_read(
                        &object, "missing", &field_data, &field_value),
                    CMETA_INVALID_ARGUMENT);
        check_null(field_data);
        check_null(field_value);
    }

    it("resolves methods against the bound native receiver type") {
        object_box box = {0};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        cmeta_receiver_resolution resolution = CMETA_RECEIVER_RESOLUTION_INIT;
        const cmeta_type_desc *arguments[] = {&cmeta_type_int};

        check_equal(cmeta_object_borrow(
                        &object, &box, &object_box_data, &object_method_set),
                    CMETA_OK);
        check_equal(cmeta_object_method_resolve(
                        &object, "ObjectBox", "add",
                        arguments, 1u, &resolution),
                    CMETA_RECEIVER_RESOLVE_OK);
        check_true(resolution.method == &object_methods[0]);
        check_equal(resolution.argument_index, CMETA_RECEIVER_ARGUMENT_NONE);

        resolution = (cmeta_receiver_resolution)CMETA_RECEIVER_RESOLUTION_INIT;
        check_equal(cmeta_object_method_resolve(
                        &object, NULL, "missing",
                        arguments, 1u, &resolution),
                    CMETA_RECEIVER_RESOLVE_METHOD_NOT_FOUND);
        check_null(resolution.method);
    }

    it("binds and invokes a resolved method on the same native instance") {
        object_box box = {10};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        cmeta_receiver_resolution resolution = CMETA_RECEIVER_RESOLUTION_INIT;
        cmeta_invokable invokable = CMETA_INVOKABLE_INIT;
        const cmeta_type_desc *arguments[] = {&cmeta_type_int};
        const void *invoke_args[1];
        const cmeta_data_desc *field_data = NULL;
        const void *field_value = NULL;
        int delta = 7;
        int result = 0;

        invoke_args[0] = &delta;
        check_true(cmeta_object_method_provider_valid(&object_method_provider));
        check_equal(cmeta_object_borrow_with_provider(
                        &object, &box, &object_box_data,
                        &object_method_provider),
                    CMETA_OK);
        check_true(object.methods == &object_method_set);
        check_true(object.method_provider == &object_method_provider);

        check_equal(cmeta_object_method_resolve(
                        &object, "ObjectBox", "add",
                        arguments, 1u, &resolution),
                    CMETA_RECEIVER_RESOLVE_OK);
        check_true(resolution.method == &object_methods[0]);

        check_equal(cmeta_object_method_invokable_bind(
                        &object, resolution.method, &invokable),
                    CMETA_OK);
        check_equal(cmeta_invokable_invoke(
                        &invokable, &result, invoke_args),
                    CMETA_OK);
        check_equal(result, 17);
        check_equal(box.value, 17);

        check_equal(cmeta_object_field_read(
                        &object, "value", &field_data, &field_value),
                    CMETA_OK);
        check_true(field_data == &cmeta_data_int);
        check_true(field_value == &box.value);
        check_equal(*(const int *)field_value, 17);

        cmeta_object_release(&object);
        check_equal(box.value, 17);
    }

    it("does not execute reflected methods without an executable provider") {
        object_box box = {1};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        cmeta_invokable invokable = CMETA_INVOKABLE_INIT;

        check_equal(cmeta_object_borrow(
                        &object, &box, &object_box_data, &object_method_set),
                    CMETA_OK);
        check_equal(cmeta_object_method_invokable_bind(
                        &object, &object_methods[0], &invokable),
                    CMETA_TRAIT_MISSING);
        check_equal(box.value, 1);
    }

    it("rejects method entries that are not provider capability tokens") {
        object_box box = {1};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        cmeta_receiver_method copied = object_methods[0];
        cmeta_invokable invokable = CMETA_INVOKABLE_INIT;

        check_equal(cmeta_object_borrow_with_provider(
                        &object, &box, &object_box_data,
                        &object_method_provider),
                    CMETA_OK);
        check_true(cmeta_receiver_method_reflection_valid(&copied));
        check_equal(cmeta_object_method_invokable_bind(
                        &object, &copied, &invokable),
                    CMETA_TRAIT_MISSING);
        check_equal(box.value, 1);
    }

    it("allows data-only borrowed objects") {
        object_box box = {3};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;

        check_equal(cmeta_object_borrow(
                        &object, &box, &object_box_data, NULL),
                    CMETA_OK);
        check_true(cmeta_object_ref_valid(&object));
        check_null(object.methods);
        {
            cmeta_receiver_resolution resolution =
                CMETA_RECEIVER_RESOLUTION_INIT;
            check_equal(cmeta_object_method_resolve(
                            &object, NULL, "add", NULL, 0u, &resolution),
                        CMETA_RECEIVER_RESOLVE_INVALID_METHOD_SET);
        }
        cmeta_object_release(&object);
        check_equal(box.value, 3);
    }

    it("rejects method metadata for a different receiver type") {
        object_box box = {0};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        cmeta_data_desc wrong_data = object_box_data;

        wrong_data.storage_type = &cmeta_type_int;
        check_true(cmeta_data_desc_valid(&wrong_data));
        check_equal(cmeta_object_borrow(
                        &object, &box, &wrong_data, &object_method_set),
                    CMETA_TYPE_MISMATCH);
        check_false(cmeta_object_ref_valid(&object));
    }

    it("rejects descriptors without concrete native storage") {
        object_box box = {0};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;

        check_true(cmeta_data_desc_valid(&cmeta_data_sequence));
        check_null(cmeta_data_sequence.storage_type);
        check_equal(cmeta_object_borrow(
                        &object, &box, &cmeta_data_sequence, NULL),
                    CMETA_TRAIT_MISSING);
        check_false(cmeta_object_ref_valid(&object));
    }

    it("fails closed on malformed method metadata") {
        object_box box = {0};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        cmeta_receiver_method_set malformed = object_method_set;

        malformed.size = 0u;
        check_equal(cmeta_object_borrow(
                        &object, &box, &object_box_data, &malformed),
                    CMETA_INVALID_ARGUMENT);
        check_false(cmeta_object_ref_valid(&object));

        check_equal(cmeta_object_borrow(
                        NULL, &box, &object_box_data, NULL),
                    CMETA_INVALID_ARGUMENT);
    }
}
