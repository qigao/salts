#include <cmeta/invokable.h>
#include <cmeta/object.h>
#include <cmeta/object_interface.h>
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

#define OBJECT_READER_METHODS(X,I) \
    X(I,R0,int,value,_)

CMETA_INTERFACE(object_reader, OBJECT_READER_METHODS);

static int object_box_reader_value(void *self) {
    return ((object_box *)self)->value;
}

CMETA_IMPLEMENTS(object_reader, object_box_reader, 0u,
    .value = object_box_reader_value
);

CMETA_OBJECT_INTERFACE_ADAPTER(object_reader);

#define OBJECT_WRITER_METHODS(X,I) \
    X(I,R1,int,add,int,delta)

CMETA_INTERFACE(object_writer, OBJECT_WRITER_METHODS);

static int object_box_writer_add(void *self, int delta) {
    object_box *box = (object_box *)self;
    box->value += delta;
    return box->value;
}

CMETA_IMPLEMENTS(object_writer, object_box_writer, 0u,
    .add = object_box_writer_add
);

CMETA_OBJECT_INTERFACE_ADAPTER(object_writer);

#define OBJECT_WAITABLE_METHODS(X,I) \
    X(I,R0,int,state,_)

CMETA_INTERFACE(object_waitable, OBJECT_WAITABLE_METHODS);
CMETA_OBJECT_INTERFACE_ADAPTER(object_waitable);

#define OBJECT_OWNER_METHODS(X,I) \
    X(I,D0,void,destroy,_)

CMETA_INTERFACE(object_owner, OBJECT_OWNER_METHODS);
CMETA_OBJECT_INTERFACE_ADAPTER(object_owner);

static size_t object_interface_project_calls;

static cmeta_status object_box_interface_project(
    void *context,
    const cmeta_object_ref *object,
    const cmeta_interface_desc *expected,
    cmeta_interface_projection *out) {
    (void)context;
    ++object_interface_project_calls;
    if (object == NULL || object->object == NULL || expected == NULL ||
        out == NULL)
        return CMETA_INVALID_ARGUMENT;

    *out = (cmeta_interface_projection)CMETA_INTERFACE_PROJECTION_INIT;
    if (cmeta_interface_desc_equal(expected, object_reader_interface())) {
        out->interface = object_reader_interface();
        out->self = object->object;
        out->dispatch = &object_box_reader_vtable;
        return CMETA_OK;
    }
    if (cmeta_interface_desc_equal(expected, object_writer_interface())) {
        out->interface = object_writer_interface();
        out->self = object->object;
        out->dispatch = &object_box_writer_vtable;
        return CMETA_OK;
    }
    return CMETA_TRAIT_MISSING;
}

static const cmeta_object_interface_provider object_interface_provider = {
    .size = sizeof(cmeta_object_interface_provider),
    .context = NULL,
    .project = object_box_interface_project
};

static cmeta_status object_box_foreign_interface_project(
    void *context,
    const cmeta_object_ref *object,
    const cmeta_interface_desc *expected,
    cmeta_interface_projection *out) {
    (void)context;
    (void)expected;
    if (object == NULL || object->object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    *out = (cmeta_interface_projection)CMETA_INTERFACE_PROJECTION_INIT;
    out->interface = object_reader_interface();
    out->self = object->object;
    out->dispatch = &object_box_reader_vtable;
    return CMETA_OK;
}

static const cmeta_object_interface_provider object_foreign_interface_provider = {
    .size = sizeof(cmeta_object_interface_provider),
    .context = NULL,
    .project = object_box_foreign_interface_project
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
    .properties = CMETA_PROP_NONE,
    .result_flags = CMETA_RESULT_UNKNOWN
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

static const cmeta_receiver_operation object_methods[] = {
    {
        .name = "add",
        .abi = &object_add_abi
    }
};

static const cmeta_receiver_operation_set object_method_set = {
    .size = sizeof(cmeta_receiver_operation_set),
    .receiver_type = &object_box_type,
    .operations = object_methods,
    .operation_count = 1u,
    .owner = NULL
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
    .properties = CMETA_PROP_NONE,
    .result_flags = CMETA_RESULT_UNKNOWN
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
    void *context, void *object, const cmeta_receiver_operation *method,
    cmeta_object_operation_binding *out) {
    object_box *receiver = (object_box *)object;
    cmeta_callable callable = object_add_callable_shape;

    (void)context;
    if (receiver == NULL || method != &object_methods[0] || out == NULL)
        return CMETA_INVALID_ARGUMENT;

    *out = (cmeta_object_operation_binding)CMETA_OBJECT_OPERATION_BINDING_INIT;
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

static const cmeta_object_operation_provider object_method_provider = {
    .size = sizeof(cmeta_object_operation_provider),
    .operations = &object_method_set,
    .context = NULL,
    .bind = object_box_method_bind
};


typedef struct object_field_assignments {
    int count;
} object_field_assignments;

static object_field_assignments object_field_counts = {0};

static cmeta_status object_box_field_assign(
    void *context, void *object, const cmeta_data_field_desc *field,
    const void *value) {
    object_field_assignments *counts =
        (object_field_assignments *)context;
    object_box *box = (object_box *)object;

    if (box == NULL || field == NULL || value == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (field != &object_box_data_fields[0])
        return CMETA_TRAIT_MISSING;
    if (counts != NULL)
        ++counts->count;
    box->value = *(const int *)value;
    return CMETA_OK;
}

static const cmeta_object_field_provider object_field_provider = {
    .size = sizeof(cmeta_object_field_provider),
    .data = &object_box_data,
    .context = &object_field_counts,
    .assign = object_box_field_assign,
    .read = NULL
};

typedef struct dynamic_object_box {
    int marker;
    int slots[2];
} dynamic_object_box;

static const cmeta_type_desc dynamic_object_box_type = {
    .name = "dynamic_object_box",
    .size = sizeof(dynamic_object_box),
    .align = _Alignof(dynamic_object_box),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_field_desc dynamic_object_layout_fields[] = {
    {
        .name = "value",
        .type_name = "int",
        .offset = CMETA_FIELD_DYNAMIC_OFFSET,
        .size = sizeof(int),
        .align = _Alignof(int),
        .type = &cmeta_type_int,
        .declared_type = NULL
    }
};

static const cmeta_struct_desc dynamic_object_layout = {
    .name = "dynamic_object_box",
    .size = sizeof(dynamic_object_box),
    .align = _Alignof(dynamic_object_box),
    .fields = dynamic_object_layout_fields,
    .field_count = 1u
};

static const cmeta_data_field_desc dynamic_object_data_fields[] = {
    {
        .stable_id = "test.dynamic_object_box.value",
        .name = "value",
        .offset = CMETA_FIELD_DYNAMIC_OFFSET,
        .value = &cmeta_data_int
    }
};

static const cmeta_data_struct_shape dynamic_object_shape = {
    .layout = &dynamic_object_layout,
    .fields = dynamic_object_data_fields,
    .field_count = 1u
};

static const cmeta_data_desc dynamic_object_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.dynamic_object_box.data",
    .display_name = "dynamic_object_box",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &dynamic_object_box_type,
    .shape = &dynamic_object_shape,
    .buffer_ops = NULL,
    .enum_ops = NULL,
    .variant_ops = NULL,
    .fixed_ops = NULL,
    .enum_bits_ops = NULL,
    .collection_ops = NULL,
    .map_ops = NULL,
    .construct_ops = NULL
};

static cmeta_status dynamic_object_field_read(
    void *context, const void *object, const cmeta_data_field_desc *field,
    const void **out_value) {
    const dynamic_object_box *box = (const dynamic_object_box *)object;

    (void)context;
    if (box == NULL || field == NULL || out_value == NULL)
        return CMETA_INVALID_ARGUMENT;
    *out_value = NULL;
    if (field != &dynamic_object_data_fields[0])
        return CMETA_TRAIT_MISSING;
    *out_value = &box->slots[1];
    return CMETA_OK;
}

static cmeta_status dynamic_object_field_assign(
    void *context, void *object, const cmeta_data_field_desc *field,
    const void *value) {
    dynamic_object_box *box = (dynamic_object_box *)object;

    (void)context;
    if (box == NULL || field == NULL || value == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (field != &dynamic_object_data_fields[0])
        return CMETA_TRAIT_MISSING;
    box->slots[1] = *(const int *)value;
    return CMETA_OK;
}

static const cmeta_object_field_provider dynamic_object_field_provider = {
    .size = sizeof(cmeta_object_field_provider),
    .data = &dynamic_object_data,
    .context = NULL,
    .assign = dynamic_object_field_assign,
    .read = dynamic_object_field_read
};

static const cmeta_object_field_provider dynamic_object_read_only_provider = {
    .size = sizeof(cmeta_object_field_provider),
    .data = &dynamic_object_data,
    .context = NULL,
    .assign = NULL,
    .read = dynamic_object_field_read
};

typedef struct object_lifecycle_counts {
    int retains;
    int releases;
    int destroys;
    bool fail_retain;
} object_lifecycle_counts;

static cmeta_status object_test_retain(void *context, void *object) {
    object_lifecycle_counts *counts = (object_lifecycle_counts *)context;
    if (counts == NULL || object == NULL)
        return CMETA_INVALID_ARGUMENT;
    counts->retains += 1;
    return counts->fail_retain ? CMETA_CALLBACK_ERROR : CMETA_OK;
}

static void object_test_release(void *context, void *object) {
    object_lifecycle_counts *counts = (object_lifecycle_counts *)context;
    if (counts != NULL && object != NULL)
        counts->releases += 1;
}

static void object_test_destroy(void *context, void *object) {
    object_lifecycle_counts *counts = (object_lifecycle_counts *)context;
    if (counts != NULL && object != NULL)
        counts->destroys += 1;
}

spec("CMeta ObjectRef Interface projection") {
    it("projects multiple borrowed capabilities over one native identity") {
        object_box box = {7};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        object_reader reader = object_reader_bind(NULL, NULL);
        object_writer writer = object_writer_bind(NULL, NULL);

        object_interface_project_calls = 0u;
        check_true(cmeta_object_interface_provider_valid(
            &object_interface_provider));
        check_equal(cmeta_object_borrow(
                        &object, &box, &object_box_data, NULL),
                    CMETA_OK);

        check_equal(object_reader_borrow_from_object(
                        &object, &object_interface_provider, &reader),
                    CMETA_OK);
        check_equal(object_writer_borrow_from_object(
                        &object, &object_interface_provider, &writer),
                    CMETA_OK);
        check_true(object_reader_valid(&reader));
        check_true(object_writer_valid(&writer));
        check_true(reader.self == &box);
        check_true(writer.self == &box);
        check_equal(object_interface_project_calls, (size_t)2u);

        check_equal(object_reader_value(&reader), 7);
        check_equal(object_writer_add(&writer, 5), 12);
        check_equal(object_reader_value(&reader), 12);
        check_equal(box.value, 12);

        cmeta_object_release(&object);
    }

    it("fails closed for unsupported and foreign capabilities") {
        object_box box = {3};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        object_waitable waitable = object_waitable_bind(NULL, NULL);
        object_writer writer = object_writer_bind(NULL, NULL);

        check_equal(cmeta_object_borrow(
                        &object, &box, &object_box_data, NULL),
                    CMETA_OK);
        check_equal(object_waitable_borrow_from_object(
                        &object, &object_interface_provider, &waitable),
                    CMETA_TRAIT_MISSING);
        check_false(object_waitable_valid(&waitable));

        check_equal(object_writer_borrow_from_object(
                        &object, &object_foreign_interface_provider, &writer),
                    CMETA_CALLBACK_ERROR);
        check_false(object_writer_valid(&writer));

        cmeta_object_release(&object);
    }

    it("rejects owning Interface projection before provider dispatch") {
        object_box box = {9};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        object_owner owner = object_owner_bind(NULL, NULL);
        size_t calls_before;

        check_equal(cmeta_object_borrow(
                        &object, &box, &object_box_data, NULL),
                    CMETA_OK);
        calls_before = object_interface_project_calls;
        check_true(cmeta_interface_desc_has_owning_method(
            object_owner_interface()));
        check_equal(object_owner_borrow_from_object(
                        &object, &object_interface_provider, &owner),
                    CMETA_TRAIT_MISSING);
        check_equal(object_interface_project_calls, calls_before);
        check_false(object_owner_valid(&owner));
        check_equal(box.value, 9);

        cmeta_object_release(&object);
    }
}

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
        check_true(object.operations == &object_method_set);
        check_null(object.operation_provider);
        check_equal(object.lifetime, CMETA_OBJECT_LIFETIME_BORROWED);

        ((object_box *)object.object)->value = 11;
        check_equal(box.value, 11);

        check_equal(object_box_add(&box, 5), 16);
        check_equal(((object_box *)object.object)->value, 16);

        cmeta_object_release(&object);
        check_false(cmeta_object_ref_valid(&object));
        check_null(object.object);
        check_null(object.data);
        check_null(object.operations);
        check_null(object.operation_provider);
        check_equal(object.lifetime, CMETA_OBJECT_LIFETIME_NONE);
        check_equal(box.value, 16);
    }

    it("rejects malformed object lifecycle metadata before callbacks") {
        object_lifecycle_counts counts = {0};
        cmeta_object_lifecycle lifecycle = {
            .size = 0u,
            .context = &counts,
            .retain = object_test_retain,
            .release = object_test_release,
            .destroy = object_test_destroy
        };

        check_false(cmeta_object_lifecycle_valid(&lifecycle));
        check_equal(counts.retains, 0);
        check_equal(counts.releases, 0);
        check_equal(counts.destroys, 0);
    }

    it("retains and releases shared object ownership exactly once") {
        object_box box = {4};
        object_lifecycle_counts counts = {0};
        cmeta_object_lifecycle lifecycle = {
            .size = sizeof(cmeta_object_lifecycle),
            .context = &counts,
            .retain = object_test_retain,
            .release = object_test_release,
            .destroy = NULL
        };
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;

        check_true(cmeta_object_lifecycle_valid(&lifecycle));
        check_equal(cmeta_object_borrow(
                        &object, &box, &object_box_data, NULL),
                    CMETA_OK);
        check_equal(cmeta_object_share(&object, &lifecycle), CMETA_OK);
        check_true(cmeta_object_ref_valid(&object));
        check_equal(object.lifetime, CMETA_OBJECT_LIFETIME_SHARED);
        check_true(object.lifecycle == &lifecycle);
        check_equal(counts.retains, 1);
        check_equal(counts.releases, 0);

        cmeta_object_release(&object);
        check_equal(counts.retains, 1);
        check_equal(counts.releases, 1);
        check_equal(counts.destroys, 0);
        cmeta_object_release(&object);
        check_equal(counts.releases, 1);
    }

    it("destroys transferred owned object exactly once") {
        object_box box = {5};
        object_lifecycle_counts counts = {0};
        cmeta_object_lifecycle lifecycle = {
            .size = sizeof(cmeta_object_lifecycle),
            .context = &counts,
            .retain = NULL,
            .release = NULL,
            .destroy = object_test_destroy
        };
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;

        check_true(cmeta_object_lifecycle_valid(&lifecycle));
        check_equal(cmeta_object_borrow(
                        &object, &box, &object_box_data, NULL),
                    CMETA_OK);
        check_equal(cmeta_object_take(&object, &lifecycle), CMETA_OK);
        check_true(cmeta_object_ref_valid(&object));
        check_equal(object.lifetime, CMETA_OBJECT_LIFETIME_OWNED);
        check_equal(counts.destroys, 0);

        cmeta_object_release(&object);
        check_equal(counts.destroys, 1);
        cmeta_object_release(&object);
        check_equal(counts.destroys, 1);
    }

    it("keeps a borrowed handle unchanged when shared retain fails") {
        object_box box = {6};
        object_lifecycle_counts counts = {
            .retains = 0,
            .releases = 0,
            .destroys = 0,
            .fail_retain = true
        };
        cmeta_object_lifecycle lifecycle = {
            .size = sizeof(cmeta_object_lifecycle),
            .context = &counts,
            .retain = object_test_retain,
            .release = object_test_release,
            .destroy = NULL
        };
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;

        check_equal(cmeta_object_borrow(
                        &object, &box, &object_box_data, NULL),
                    CMETA_OK);
        check_equal(cmeta_object_share(&object, &lifecycle),
                    CMETA_CALLBACK_ERROR);
        check_true(cmeta_object_ref_valid(&object));
        check_equal(object.lifetime, CMETA_OBJECT_LIFETIME_BORROWED);
        check_null(object.lifecycle);
        check_equal(counts.retains, 1);

        cmeta_object_release(&object);
        check_equal(counts.releases, 0);
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

    it("reads provider-backed dynamic fields without fabricated offsets") {
        dynamic_object_box box = {3, {5, 9}};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        const cmeta_data_desc *field_data = NULL;
        const void *field_value = NULL;
        int next = 17;

        check_true(cmeta_data_desc_valid(&dynamic_object_data));
        check_true(cmeta_object_field_provider_valid(
            &dynamic_object_field_provider));
        check_equal(cmeta_object_borrow_with_providers(
                        &object, &box, &dynamic_object_data,
                        &dynamic_object_field_provider, NULL),
                    CMETA_OK);

        check_equal(cmeta_object_field_read(
                        &object, "value", &field_data, &field_value),
                    CMETA_OK);
        check_true(field_data == &cmeta_data_int);
        check_true(field_value == &box.slots[1]);
        check_equal(*(const int *)field_value, 9);

        box.slots[1] = 12;
        check_equal(*(const int *)field_value, 12);

        check_equal(cmeta_object_field_assign(
                        &object, "value", &cmeta_data_int, &next),
                    CMETA_OK);
        check_equal(box.marker, 3);
        check_equal(box.slots[0], 5);
        check_equal(box.slots[1], 17);

        cmeta_object_release(&object);
    }

    it("fails closed for dynamic fields without a read provider") {
        dynamic_object_box box = {1, {2, 3}};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        const cmeta_data_desc *field_data = &cmeta_data_long;
        const void *field_value = &box;

        check_equal(cmeta_object_borrow(
                        &object, &box, &dynamic_object_data, NULL),
                    CMETA_OK);
        check_equal(cmeta_object_field_read(
                        &object, "value", &field_data, &field_value),
                    CMETA_TRAIT_MISSING);
        check_null(field_data);
        check_null(field_value);
        cmeta_object_release(&object);
    }

    it("supports read-only dynamic field providers") {
        dynamic_object_box box = {1, {2, 8}};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        const cmeta_data_desc *field_data = NULL;
        const void *field_value = NULL;
        int next = 11;

        check_true(cmeta_object_field_provider_valid(
            &dynamic_object_read_only_provider));
        check_equal(cmeta_object_borrow_with_providers(
                        &object, &box, &dynamic_object_data,
                        &dynamic_object_read_only_provider, NULL),
                    CMETA_OK);
        check_equal(cmeta_object_field_read(
                        &object, "value", &field_data, &field_value),
                    CMETA_OK);
        check_true(field_value == &box.slots[1]);
        check_equal(*(const int *)field_value, 8);
        check_equal(cmeta_object_field_assign(
                        &object, "value", &cmeta_data_int, &next),
                    CMETA_TRAIT_MISSING);
        check_equal(box.slots[1], 8);
        cmeta_object_release(&object);
    }

    it("assigns reflected fields only through explicit authority") {
        object_box box = {4};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        int next = 21;
        long wrong = 99;
        int before;

        object_field_counts.count = 0;
        check_true(cmeta_object_field_provider_valid(&object_field_provider));
        check_equal(cmeta_object_borrow_with_providers(
                        &object, &box, &object_box_data,
                        &object_field_provider, &object_method_provider),
                    CMETA_OK);
        check_true(object.field_provider == &object_field_provider);
        check_true(object.operation_provider == &object_method_provider);

        check_equal(cmeta_object_field_assign(
                        &object, "value", &cmeta_data_int, &next),
                    CMETA_OK);
        check_equal(box.value, 21);
        check_equal(object_field_counts.count, 1);

        before = object_field_counts.count;
        check_equal(cmeta_object_field_assign(
                        &object, "value", &cmeta_data_long, &wrong),
                    CMETA_TYPE_MISMATCH);
        check_equal(object_field_counts.count, before);
        check_equal(box.value, 21);

        cmeta_object_release(&object);
    }

    it("does not infer reflected field writability without a provider") {
        object_box box = {8};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        int next = 42;

        check_equal(cmeta_object_borrow(
                        &object, &box, &object_box_data, NULL),
                    CMETA_OK);
        check_equal(cmeta_object_field_assign(
                        &object, "value", &cmeta_data_int, &next),
                    CMETA_TRAIT_MISSING);
        check_equal(box.value, 8);
        cmeta_object_release(&object);
    }

    it("does not lend field capability to a foreign data descriptor") {
        object_box box = {1};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        cmeta_data_desc foreign = object_box_data;

        check_true(cmeta_data_desc_equal(&foreign, &object_box_data));
        check_equal(cmeta_object_borrow_with_providers(
                        &object, &box, &foreign,
                        &object_field_provider, NULL),
                    CMETA_INVALID_ARGUMENT);
        check_false(cmeta_object_ref_valid(&object));
    }

    it("resolves operations against the bound native receiver type") {
        object_box box = {0};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        cmeta_receiver_resolution resolution = CMETA_RECEIVER_RESOLUTION_INIT;
        const cmeta_type_desc *arguments[] = {&cmeta_type_int};

        check_equal(cmeta_object_borrow(
                        &object, &box, &object_box_data, &object_method_set),
                    CMETA_OK);
        check_equal(cmeta_object_operation_resolve(
                        &object, NULL, "add",
                        arguments, 1u, &resolution),
                    CMETA_RECEIVER_RESOLVE_OK);
        check_true(resolution.operation == &object_methods[0]);
        check_equal(resolution.argument_index, CMETA_RECEIVER_ARGUMENT_NONE);

        resolution = (cmeta_receiver_resolution)CMETA_RECEIVER_RESOLUTION_INIT;
        check_equal(cmeta_object_operation_resolve(
                        &object, NULL, "missing",
                        arguments, 1u, &resolution),
                    CMETA_RECEIVER_RESOLVE_OPERATION_NOT_FOUND);
        check_null(resolution.operation);
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
        check_true(cmeta_object_operation_provider_valid(&object_method_provider));
        check_equal(cmeta_object_borrow_with_provider(
                        &object, &box, &object_box_data,
                        &object_method_provider),
                    CMETA_OK);
        check_true(object.operations == &object_method_set);
        check_true(object.operation_provider == &object_method_provider);

        check_equal(cmeta_object_operation_resolve(
                        &object, NULL, "add",
                        arguments, 1u, &resolution),
                    CMETA_RECEIVER_RESOLVE_OK);
        check_true(resolution.operation == &object_methods[0]);

        check_equal(cmeta_object_operation_invokable_bind(
                        &object, resolution.operation, &invokable),
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

    it("does not execute reflected operations without an executable provider") {
        object_box box = {1};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        cmeta_invokable invokable = CMETA_INVOKABLE_INIT;

        check_equal(cmeta_object_borrow(
                        &object, &box, &object_box_data, &object_method_set),
                    CMETA_OK);
        check_equal(cmeta_object_operation_invokable_bind(
                        &object, &object_methods[0], &invokable),
                    CMETA_TRAIT_MISSING);
        check_equal(box.value, 1);
    }

    it("rejects method entries that are not provider capability tokens") {
        object_box box = {1};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        cmeta_receiver_operation copied = object_methods[0];
        cmeta_invokable invokable = CMETA_INVOKABLE_INIT;

        check_equal(cmeta_object_borrow_with_provider(
                        &object, &box, &object_box_data,
                        &object_method_provider),
                    CMETA_OK);
        check_true(cmeta_receiver_operation_reflection_valid(&copied));
        check_equal(cmeta_object_operation_invokable_bind(
                        &object, &copied, &invokable),
                    CMETA_INVALID_ARGUMENT);
        check_equal(box.value, 1);
    }

    it("allows data-only borrowed objects") {
        object_box box = {3};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;

        check_equal(cmeta_object_borrow(
                        &object, &box, &object_box_data, NULL),
                    CMETA_OK);
        check_true(cmeta_object_ref_valid(&object));
        check_null(object.operations);
        {
            cmeta_receiver_resolution resolution =
                CMETA_RECEIVER_RESOLUTION_INIT;
            check_equal(cmeta_object_operation_resolve(
                            &object, NULL, "add", NULL, 0u, &resolution),
                        CMETA_RECEIVER_RESOLVE_INVALID_OPERATION_SET);
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
        cmeta_receiver_operation_set malformed = object_method_set;

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
