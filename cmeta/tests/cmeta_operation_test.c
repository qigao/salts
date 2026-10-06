#include <cmeta/operation.h>
#include "tinytest.h"

typedef struct operation_box {
    int value;
} operation_box;

static const cmeta_type_desc operation_box_type = {
    "operation_box", sizeof(operation_box), _Alignof(operation_box),
    CMETA_T_OBJECT, NULL, NULL, NULL
};

static const cmeta_type_desc operation_box_ptr_type = {
    "operation_box *", sizeof(operation_box *), _Alignof(operation_box *),
    CMETA_T_POINTER, &operation_box_type, NULL, NULL
};

static const cmeta_param_desc operation_add_params[] = {
    {
        sizeof(cmeta_param_desc), "self", &operation_box_ptr_type,
        CMETA_PARAM_INOUT | CMETA_PARAM_BORROWED | CMETA_PARAM_RECEIVER
    },
    {
        sizeof(cmeta_param_desc), "value", &cmeta_type_int, CMETA_PARAM_IN
    }
};

static const cmeta_function_desc operation_add_function = {
    sizeof(cmeta_function_desc), "operation_box_add", &cmeta_type_int,
    operation_add_params, 2u,
    CMETA_EFFECT_STATEFUL | CMETA_EFFECT_MAY_FAIL, CMETA_PROP_NONE,
    CMETA_RESULT_UNKNOWN
};

static const cmeta_abi_carrier operation_add_param_abi[] = {
    CMETA_ABI_OBJECT_POINTER, CMETA_ABI_SCALAR
};

static const cmeta_function_abi_desc operation_add_abi = {
    sizeof(cmeta_function_abi_desc), &operation_add_function,
    CMETA_ABI_SCALAR, operation_add_param_abi, 2u
};

static const cmeta_receiver_operation operation_entries[] = {
    {"add", &operation_add_abi}
};

static const cmeta_receiver_operation_set operation_set = {
    sizeof(cmeta_receiver_operation_set), &operation_box_type,
    operation_entries, 1u, NULL
};

static const cmeta_generic_desc operation_box_generic =
    CMETA_GENERIC_DESC_INIT(
        "test.MethodBox", "MethodBox", 1u, 1u, CMETA_GENERIC_HANDLE);
static const cmeta_generic_desc operation_other_generic =
    CMETA_GENERIC_DESC_INIT(
        "test.Other", "Other", 1u, 1u, CMETA_GENERIC_HANDLE);

spec("CMeta receiver operation set") {
    it("validates and finds a canonical receiver operation") {
        const cmeta_receiver_operation *operation;

        check_true(cmeta_receiver_operation_reflection_valid(&operation_entries[0]));
        check_true(cmeta_receiver_operation_set_valid(&operation_set));
        check_null(operation_set.owner);
        operation = cmeta_receiver_operation_find(&operation_set, "add");
        check_not_null(operation);
        check_equal(operation->name, "add");
        check_true(operation->abi->function == &operation_add_function);
        check_true(operation->abi == &operation_add_abi);
        check_null(cmeta_receiver_operation_find(&operation_set, "missing"));
    }

    it("resolves ordinary and generic owner semantics precisely") {
        cmeta_receiver_resolution resolution = CMETA_RECEIVER_RESOLUTION_INIT;
        const cmeta_type_desc *one_int[] = {&cmeta_type_int};
        const cmeta_type_desc *one_long[] = {&cmeta_type_long};
        cmeta_type_desc other_type = operation_box_type;
        cmeta_receiver_operation_set generic_set = operation_set;
        cmeta_generic_desc generic_clone = operation_box_generic;
        cmeta_receiver_resolve_status status;

        status = cmeta_receiver_operation_resolve(
            &operation_set, &operation_box_type, NULL, "add",
            one_int, 1u, &resolution);
        check_equal(status, CMETA_RECEIVER_RESOLVE_OK);
        check_true(resolution.operation == &operation_entries[0]);
        check_equal(resolution.argument_index, CMETA_RECEIVER_ARGUMENT_NONE);

        resolution = (cmeta_receiver_resolution)CMETA_RECEIVER_RESOLUTION_INIT;
        status = cmeta_receiver_operation_resolve(
            &operation_set, &operation_box_type, &operation_box_generic, "add",
            one_int, 1u, &resolution);
        check_equal(status, CMETA_RECEIVER_RESOLVE_OWNER_MISMATCH);

        generic_set.owner = &operation_box_generic;
        check_true(cmeta_receiver_operation_set_valid(&generic_set));
        check_true(cmeta_generic_desc_equal(
            generic_set.owner, &generic_clone));

        resolution = (cmeta_receiver_resolution)CMETA_RECEIVER_RESOLUTION_INIT;
        status = cmeta_receiver_operation_resolve(
            &generic_set, &operation_box_type, &generic_clone, "add",
            one_int, 1u, &resolution);
        check_equal(status, CMETA_RECEIVER_RESOLVE_OK);
        check_true(resolution.operation == &operation_entries[0]);

        resolution = (cmeta_receiver_resolution)CMETA_RECEIVER_RESOLUTION_INIT;
        status = cmeta_receiver_operation_resolve(
            &generic_set, &operation_box_type, &operation_other_generic, "add",
            one_int, 1u, &resolution);
        check_equal(status, CMETA_RECEIVER_RESOLVE_OWNER_MISMATCH);

        resolution = (cmeta_receiver_resolution)CMETA_RECEIVER_RESOLUTION_INIT;
        status = cmeta_receiver_operation_resolve(
            &operation_set, &operation_box_type, NULL, "missing",
            one_int, 1u, &resolution);
        check_equal(status, CMETA_RECEIVER_RESOLVE_OPERATION_NOT_FOUND);
        check_null(resolution.operation);

        resolution = (cmeta_receiver_resolution)CMETA_RECEIVER_RESOLUTION_INIT;
        status = cmeta_receiver_operation_resolve(
            &operation_set, &operation_box_type, NULL, "add",
            NULL, 0u, &resolution);
        check_equal(status, CMETA_RECEIVER_RESOLVE_ARITY_MISMATCH);

        resolution = (cmeta_receiver_resolution)CMETA_RECEIVER_RESOLUTION_INIT;
        status = cmeta_receiver_operation_resolve(
            &operation_set, &operation_box_type, NULL, "add",
            one_long, 1u, &resolution);
        check_equal(status, CMETA_RECEIVER_RESOLVE_ARGUMENT_TYPE_MISMATCH);
        check_equal(resolution.argument_index, (size_t)0u);
        check_null(resolution.operation);

        other_type.name = "other_method_box";
        resolution = (cmeta_receiver_resolution)CMETA_RECEIVER_RESOLUTION_INIT;
        status = cmeta_receiver_operation_resolve(
            &operation_set, &other_type, NULL, "add",
            one_int, 1u, &resolution);
        check_equal(status, CMETA_RECEIVER_RESOLVE_RECEIVER_TYPE_MISMATCH);
    }

    it("rejects malformed semantic resolution inputs") {
        cmeta_receiver_resolution resolution = CMETA_RECEIVER_RESOLUTION_INIT;
        cmeta_receiver_operation_set invalid_set = operation_set;
        const cmeta_type_desc *bad_args[] = {NULL};

        invalid_set.size = 0u;
        check_equal(
            cmeta_receiver_operation_resolve(
                &invalid_set, &operation_box_type, NULL, "add",
                NULL, 0u, &resolution),
            CMETA_RECEIVER_RESOLVE_INVALID_OPERATION_SET);

        resolution = (cmeta_receiver_resolution)CMETA_RECEIVER_RESOLUTION_INIT;
        check_equal(
            cmeta_receiver_operation_resolve(
                &operation_set, &operation_box_type, NULL, "add",
                bad_args, 1u, &resolution),
            CMETA_RECEIVER_RESOLVE_INVALID_ARGUMENT);

        resolution.size = 0u;
        check_equal(
            cmeta_receiver_operation_resolve(
                &operation_set, &operation_box_type, NULL, "add",
                NULL, 0u, &resolution),
            CMETA_RECEIVER_RESOLVE_INVALID_ARGUMENT);
    }

    it("rejects duplicate names and receiver mismatches") {
        cmeta_receiver_operation duplicates[] = {
            {"add", &operation_add_abi},
            {"add", &operation_add_abi}
        };
        cmeta_receiver_operation_set invalid = operation_set;
        cmeta_type_desc other_type = operation_box_type;

        invalid.operations = duplicates;
        invalid.operation_count = 2u;
        check_false(cmeta_receiver_operation_set_valid(&invalid));

        other_type.name = "other_box";
        invalid = operation_set;
        invalid.receiver_type = &other_type;
        check_false(cmeta_receiver_operation_set_valid(&invalid));

        {
            const cmeta_generic_desc malformed_owner = {
                .stable_id = "",
                .display_name = "Malformed",
                .min_arity = 1u,
                .max_arity = 1u,
                .category = CMETA_GENERIC_HANDLE
            };
            invalid = operation_set;
            invalid.owner = &malformed_owner;
            check_false(cmeta_receiver_operation_set_valid(&invalid));
        }
    }

    it("rejects an ABI without a canonical receiver function") {
        cmeta_param_desc parameters[] = {
            operation_add_params[0], operation_add_params[1]
        };
        cmeta_function_desc function = operation_add_function;
        cmeta_function_abi_desc abi = operation_add_abi;
        cmeta_receiver_operation operation = operation_entries[0];

        parameters[0].flags &= ~CMETA_PARAM_RECEIVER;
        function.params = parameters;
        abi.function = &function;
        operation.abi = &abi;
        check_true(cmeta_function_abi_desc_valid(&abi));
        check_false(cmeta_receiver_operation_reflection_valid(&operation));
        abi.function = NULL;
        check_false(cmeta_receiver_operation_reflection_valid(&operation));
        operation = operation_entries[0];
        operation.name = "";
        check_false(cmeta_receiver_operation_reflection_valid(&operation));
    }

    it("rejects malformed ABI and missing storage") {
        cmeta_receiver_operation_set invalid = operation_set;
        cmeta_function_abi_desc bad_abi = operation_add_abi;
        cmeta_receiver_operation bad_operation = operation_entries[0];

        invalid.size = 0u;
        check_false(cmeta_receiver_operation_set_valid(&invalid));

        invalid = operation_set;
        invalid.operations = NULL;
        check_false(cmeta_receiver_operation_set_valid(&invalid));

        bad_abi.param_count = 1u;
        bad_operation.abi = &bad_abi;
        check_false(cmeta_receiver_operation_reflection_valid(&bad_operation));
        invalid = operation_set;
        invalid.operations = &bad_operation;
        check_false(cmeta_receiver_operation_set_valid(&invalid));
    }
}
