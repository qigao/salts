#include <cmeta/method.h>
#include "tinytest.h"

typedef struct method_box {
    int value;
} method_box;

static const cmeta_type_desc method_box_type = {
    "method_box", sizeof(method_box), _Alignof(method_box),
    CMETA_T_OBJECT, NULL, NULL, NULL
};

static const cmeta_type_desc method_box_ptr_type = {
    "method_box *", sizeof(method_box *), _Alignof(method_box *),
    CMETA_T_POINTER, &method_box_type, NULL, NULL
};

static const cmeta_param_desc method_add_params[] = {
    {
        sizeof(cmeta_param_desc), "self", &method_box_ptr_type,
        CMETA_PARAM_INOUT | CMETA_PARAM_BORROWED | CMETA_PARAM_RECEIVER
    },
    {
        sizeof(cmeta_param_desc), "value", &cmeta_type_int, CMETA_PARAM_IN
    }
};

static const cmeta_function_desc method_add_function = {
    sizeof(cmeta_function_desc), "method_box_add", &cmeta_type_int,
    method_add_params, 2u,
    CMETA_EFFECT_STATEFUL | CMETA_EFFECT_MAY_FAIL, CMETA_PROP_NONE
};

static const cmeta_abi_carrier method_add_param_abi[] = {
    CMETA_ABI_OBJECT_POINTER, CMETA_ABI_SCALAR
};

static const cmeta_function_abi_desc method_add_abi = {
    sizeof(cmeta_function_abi_desc), &method_add_function,
    CMETA_ABI_SCALAR, method_add_param_abi, 2u
};

static const cmeta_receiver_method method_entries[] = {
    {"add", &method_add_function, &method_add_abi}
};

static const cmeta_receiver_method_set method_set = {
    sizeof(cmeta_receiver_method_set), &method_box_type,
    method_entries, 1u
};

spec("CMeta receiver method set") {
    it("validates and finds a canonical receiver method") {
        const cmeta_receiver_method *method;

        check_true(cmeta_receiver_method_set_valid(&method_set));
        method = cmeta_receiver_method_find(&method_set, "add");
        check_not_null(method);
        check_equal(method->name, "add");
        check_true(method->function == &method_add_function);
        check_true(method->abi == &method_add_abi);
        check_null(cmeta_receiver_method_find(&method_set, "missing"));
    }

    it("resolves receiver call semantics with precise diagnostics") {
        cmeta_receiver_resolution resolution = CMETA_RECEIVER_RESOLUTION_INIT;
        const cmeta_type_desc *one_int[] = {&cmeta_type_int};
        const cmeta_type_desc *one_long[] = {&cmeta_type_long};
        cmeta_type_desc other_type = method_box_type;
        cmeta_receiver_resolve_status status;

        status = cmeta_receiver_method_resolve(
            &method_set, &method_box_type, "add",
            one_int, 1u, &resolution);
        check_equal(status, CMETA_RECEIVER_RESOLVE_OK);
        check_true(resolution.method == &method_entries[0]);
        check_equal(resolution.argument_index, CMETA_RECEIVER_ARGUMENT_NONE);

        resolution = (cmeta_receiver_resolution)CMETA_RECEIVER_RESOLUTION_INIT;
        status = cmeta_receiver_method_resolve(
            &method_set, &method_box_type, "missing",
            one_int, 1u, &resolution);
        check_equal(status, CMETA_RECEIVER_RESOLVE_METHOD_NOT_FOUND);
        check_null(resolution.method);

        resolution = (cmeta_receiver_resolution)CMETA_RECEIVER_RESOLUTION_INIT;
        status = cmeta_receiver_method_resolve(
            &method_set, &method_box_type, "add",
            NULL, 0u, &resolution);
        check_equal(status, CMETA_RECEIVER_RESOLVE_ARITY_MISMATCH);

        resolution = (cmeta_receiver_resolution)CMETA_RECEIVER_RESOLUTION_INIT;
        status = cmeta_receiver_method_resolve(
            &method_set, &method_box_type, "add",
            one_long, 1u, &resolution);
        check_equal(status, CMETA_RECEIVER_RESOLVE_ARGUMENT_TYPE_MISMATCH);
        check_equal(resolution.argument_index, (size_t)0u);
        check_null(resolution.method);

        other_type.name = "other_method_box";
        resolution = (cmeta_receiver_resolution)CMETA_RECEIVER_RESOLUTION_INIT;
        status = cmeta_receiver_method_resolve(
            &method_set, &other_type, "add",
            one_int, 1u, &resolution);
        check_equal(status, CMETA_RECEIVER_RESOLVE_RECEIVER_TYPE_MISMATCH);
    }

    it("rejects malformed semantic resolution inputs") {
        cmeta_receiver_resolution resolution = CMETA_RECEIVER_RESOLUTION_INIT;
        cmeta_receiver_method_set invalid_set = method_set;
        const cmeta_type_desc *bad_args[] = {NULL};

        invalid_set.size = 0u;
        check_equal(
            cmeta_receiver_method_resolve(
                &invalid_set, &method_box_type, "add",
                NULL, 0u, &resolution),
            CMETA_RECEIVER_RESOLVE_INVALID_METHOD_SET);

        resolution = (cmeta_receiver_resolution)CMETA_RECEIVER_RESOLUTION_INIT;
        check_equal(
            cmeta_receiver_method_resolve(
                &method_set, &method_box_type, "add",
                bad_args, 1u, &resolution),
            CMETA_RECEIVER_RESOLVE_INVALID_ARGUMENT);

        resolution.size = 0u;
        check_equal(
            cmeta_receiver_method_resolve(
                &method_set, &method_box_type, "add",
                NULL, 0u, &resolution),
            CMETA_RECEIVER_RESOLVE_INVALID_ARGUMENT);
    }

    it("rejects duplicate names and receiver mismatches") {
        cmeta_receiver_method duplicates[] = {
            {"add", &method_add_function, &method_add_abi},
            {"add", &method_add_function, &method_add_abi}
        };
        cmeta_receiver_method_set invalid = method_set;
        cmeta_type_desc other_type = method_box_type;

        invalid.methods = duplicates;
        invalid.method_count = 2u;
        check_false(cmeta_receiver_method_set_valid(&invalid));

        other_type.name = "other_box";
        invalid = method_set;
        invalid.receiver_type = &other_type;
        check_false(cmeta_receiver_method_set_valid(&invalid));
    }

    it("rejects malformed ABI and missing storage") {
        cmeta_receiver_method_set invalid = method_set;
        cmeta_function_abi_desc bad_abi = method_add_abi;
        cmeta_receiver_method bad_method = method_entries[0];

        invalid.size = 0u;
        check_false(cmeta_receiver_method_set_valid(&invalid));

        invalid = method_set;
        invalid.methods = NULL;
        check_false(cmeta_receiver_method_set_valid(&invalid));

        bad_abi.param_count = 1u;
        bad_method.abi = &bad_abi;
        invalid = method_set;
        invalid.methods = &bad_method;
        check_false(cmeta_receiver_method_set_valid(&invalid));
    }
}
