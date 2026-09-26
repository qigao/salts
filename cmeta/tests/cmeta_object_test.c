#include <cmeta/object.h>
#include "tinytest.h"

typedef struct object_box {
    int value;
} object_box;

static int object_box_shape_marker = 0;

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

static const cmeta_data_desc object_box_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.object_box.data",
    .display_name = "object_box",
    .kind = CMETA_DATA_CUSTOM,
    .storage_type = &object_box_type,
    .shape = &object_box_shape_marker,
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

static int object_box_add(object_box *self, int delta) {
    self->value += delta;
    return self->value;
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
        check_true(object.methods == &object_method_set);
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
        check_equal(object.lifetime, CMETA_OBJECT_LIFETIME_NONE);
        check_equal(box.value, 16);
    }

    it("allows data-only borrowed objects") {
        object_box box = {3};
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;

        check_equal(cmeta_object_borrow(
                        &object, &box, &object_box_data, NULL),
                    CMETA_OK);
        check_true(cmeta_object_ref_valid(&object));
        check_null(object.methods);
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
