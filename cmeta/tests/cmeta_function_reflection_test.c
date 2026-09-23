#include <cmeta/function.h>
#include "tinytest.h"

#include <stddef.h>

typedef struct cmeta_function_test_box {
    int value;
} cmeta_function_test_box;

typedef int (*cmeta_function_test_callback)(int);

static const cmeta_type_desc cmeta_function_test_callback_type = {
    .name = "cmeta_function_test_callback",
    .size = sizeof(cmeta_function_test_callback),
    .align = _Alignof(cmeta_function_test_callback),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_type_desc cmeta_function_test_box_type = {
    .name = "cmeta_function_test_box",
    .size = sizeof(cmeta_function_test_box),
    .align = _Alignof(cmeta_function_test_box),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_type_desc cmeta_function_test_box_ptr_type = {
    .name = "cmeta_function_test_box *",
    .size = sizeof(cmeta_function_test_box *),
    .align = _Alignof(cmeta_function_test_box *),
    .kind = CMETA_T_POINTER,
    .pointee = &cmeta_function_test_box_type,
    .traits = NULL,
    .identity = NULL
};

FunctionDecl(value, int, cmeta_function_test_sum,
    (int, left, CMETA_PARAM_IN),
    (int, right, CMETA_PARAM_IN));

Function0Decl(pure, int, cmeta_function_test_answer);

FunctionDeclAsAbi(fallible, int, &cmeta_type_int, CMETA_ABI_SCALAR,
                  cmeta_function_test_store,
    (int, value, CMETA_PARAM_IN),
    (size_t *, written, CMETA_PARAM_OUT, &cmeta_type_size_ptr,
     CMETA_ABI_OBJECT_POINTER));

FunctionDeclAsAbi(value, cmeta_function_test_box,
                  &cmeta_function_test_box_type, CMETA_ABI_AGGREGATE,
                  cmeta_function_test_box_copy,
    (cmeta_function_test_box, input, CMETA_PARAM_IN,
     &cmeta_function_test_box_type, CMETA_ABI_AGGREGATE));

FunctionDeclAsAbi(fallible, int, &cmeta_type_int, CMETA_ABI_SCALAR,
                  cmeta_function_test_box_write,
    (cmeta_function_test_box *, output, CMETA_PARAM_OUT,
     &cmeta_function_test_box_ptr_type, CMETA_ABI_OBJECT_POINTER));

FunctionDeclAsAbi(value, int, &cmeta_type_int, CMETA_ABI_SCALAR,
                  cmeta_function_test_callback_apply,
    (cmeta_function_test_callback, callback, CMETA_PARAM_IN,
     &cmeta_function_test_callback_type, CMETA_ABI_FUNCTION_POINTER),
    (int, value, CMETA_PARAM_IN));

int cmeta_function_test_sum(int left, int right) {
    return left + right;
}

int cmeta_function_test_answer(void) {
    return 42;
}

int cmeta_function_test_store(int value, size_t *written) {
    if (written == NULL)
        return -1;
    *written = (size_t)value;
    return 0;
}

cmeta_function_test_box
cmeta_function_test_box_copy(cmeta_function_test_box input) {
    return input;
}

int cmeta_function_test_box_write(cmeta_function_test_box *output) {
    if (output == NULL)
        return -1;
    output->value = 7;
    return 0;
}

int cmeta_function_test_callback_apply(cmeta_function_test_callback callback,
                                       int value) {
    return callback ? callback(value) : -1;
}

suite("CMeta function reflection") {
    it("publishes ordinary function metadata without consumer signature duplication") {
        const cmeta_function_desc *fn = FunctionMeta(cmeta_function_test_sum);
        const cmeta_param_desc *left = cmeta_function_param(fn, 0u);
        const cmeta_param_desc *right =
            cmeta_function_find_param(fn, "right");

        check_true(cmeta_function_desc_valid(fn));
        check_equal(fn->name, "cmeta_function_test_sum");
        check_true(cmeta_type_equal(fn->return_type, &cmeta_type_int));
        check_equal(fn->param_count, (size_t)2u);
        check_equal(fn->effects, CMETA_CONTRACT_EFFECTS(value));
        check_equal(fn->properties, CMETA_CONTRACT_PROPERTIES(value));
        check_equal(fn->return_abi_carrier,
                    (cmeta_abi_carrier)CMETA_ABI_SCALAR);

        check_not_null(left);
        check_equal(left->name, "left");
        check_true(cmeta_type_equal(left->type, &cmeta_type_int));
        check_equal(left->flags, (cmeta_param_flags)CMETA_PARAM_IN);
        check_equal(left->abi_carrier,
                    (cmeta_abi_carrier)CMETA_ABI_SCALAR);

        check_not_null(right);
        check_equal(right->name, "right");
        check_true(cmeta_type_equal(right->type, &cmeta_type_int));
        check_null(cmeta_function_param(fn, 2u));
        check_null(cmeta_function_find_param(fn, "missing"));
    }

    it("supports zero-parameter functions") {
        const cmeta_function_desc *fn =
            FunctionMeta(cmeta_function_test_answer);

        check_true(cmeta_function_desc_valid(fn));
        check_equal(fn->param_count, (size_t)0u);
        check_null(fn->params);
        check_true(cmeta_type_equal(fn->return_type, &cmeta_type_int));
        check_equal(fn->return_abi_carrier,
                    (cmeta_abi_carrier)CMETA_ABI_SCALAR);
        check_equal(cmeta_function_test_answer(), 42);
    }

    it("supports explicit pointer descriptors for output parameters") {
        const cmeta_function_desc *fn =
            FunctionMeta(cmeta_function_test_store);
        const cmeta_param_desc *written =
            cmeta_function_find_param(fn, "written");

        check_true(cmeta_function_desc_valid(fn));
        check_not_null(written);
        check_equal(written->flags, (cmeta_param_flags)CMETA_PARAM_OUT);
        check_true(cmeta_type_equal(written->type, &cmeta_type_size_ptr));
        check_true(written->type->kind == CMETA_T_POINTER);
        check_equal(written->abi_carrier,
                    (cmeta_abi_carrier)CMETA_ABI_OBJECT_POINTER);
        check_equal(fn->return_abi_carrier,
                    (cmeta_abi_carrier)CMETA_ABI_SCALAR);
    }

    it("supports provider-owned custom value and pointer descriptors") {
        const cmeta_function_desc *copy_fn =
            FunctionMeta(cmeta_function_test_box_copy);
        const cmeta_function_desc *write_fn =
            FunctionMeta(cmeta_function_test_box_write);
        const cmeta_param_desc *input =
            cmeta_function_find_param(copy_fn, "input");
        const cmeta_param_desc *output =
            cmeta_function_find_param(write_fn, "output");

        check_true(cmeta_function_desc_valid(copy_fn));
        check_true(cmeta_function_desc_valid(write_fn));
        check_not_null(input);
        check_not_null(output);
        check_true(cmeta_type_equal(input->type,
                                    &cmeta_function_test_box_type));
        check_true(cmeta_type_equal(output->type,
                                    &cmeta_function_test_box_ptr_type));
        check_true(cmeta_type_equal(copy_fn->return_type,
                                    &cmeta_function_test_box_type));
        check_equal(copy_fn->return_abi_carrier,
                    (cmeta_abi_carrier)CMETA_ABI_AGGREGATE);
        check_equal(input->abi_carrier,
                    (cmeta_abi_carrier)CMETA_ABI_AGGREGATE);
        check_equal(output->abi_carrier,
                    (cmeta_abi_carrier)CMETA_ABI_OBJECT_POINTER);
    }

    it("distinguishes object pointers from function pointers") {
        const cmeta_function_desc *fn =
            FunctionMeta(cmeta_function_test_callback_apply);
        const cmeta_param_desc *callback =
            cmeta_function_find_param(fn, "callback");

        check_true(cmeta_function_desc_valid(fn));
        check_not_null(callback);
        check_equal(callback->abi_carrier,
                    (cmeta_abi_carrier)CMETA_ABI_FUNCTION_POINTER);
        check_true(cmeta_abi_carrier_matches_type(
            callback->abi_carrier, callback->type));
        check_true(cmeta_abi_carrier_matches_type(
            CMETA_ABI_OBJECT_POINTER, &cmeta_function_test_box_ptr_type));
        check_false(cmeta_abi_carrier_matches_type(
            CMETA_ABI_OBJECT_POINTER, &cmeta_function_test_callback_type));
    }

    it("rejects malformed parameter metadata") {
        cmeta_param_desc param = {
            sizeof(cmeta_param_desc), "value", &cmeta_type_int,
            CMETA_PARAM_IN, CMETA_ABI_SCALAR
        };
        cmeta_param_desc pointer_param = {
            sizeof(cmeta_param_desc), "value", &cmeta_type_size_ptr,
            CMETA_PARAM_IN, CMETA_ABI_OBJECT_POINTER
        };

        check_true(cmeta_param_desc_valid(&param));

        param.size = 0u;
        check_false(cmeta_param_desc_valid(&param));
        param.size = sizeof(cmeta_param_desc);

        param.flags = CMETA_PARAM_UNKNOWN;
        check_true(cmeta_param_desc_valid(&param));
        check_false(cmeta_param_direction_known(&param));

        param.flags = CMETA_PARAM_IN;
        check_true(cmeta_param_desc_valid(&param));
        check_true(cmeta_param_direction_known(&param));

        param.flags = CMETA_PARAM_OUT;
        check_false(cmeta_param_desc_valid(&param));

        param.flags = CMETA_PARAM_IN;
        param.abi_carrier = CMETA_ABI_OBJECT_POINTER;
        check_false(cmeta_param_desc_valid(&param));
        param.abi_carrier = CMETA_ABI_SCALAR;
        check_true(cmeta_param_desc_valid(&param));

        pointer_param.flags = CMETA_PARAM_UNKNOWN;
        check_true(cmeta_param_desc_valid(&pointer_param));
        check_false(cmeta_param_direction_known(&pointer_param));

        pointer_param.flags = CMETA_PARAM_NULLABLE |
                              CMETA_PARAM_BORROWED;
        check_true(cmeta_param_desc_valid(&pointer_param));
        check_false(cmeta_param_direction_known(&pointer_param));

        pointer_param.flags = CMETA_PARAM_IN |
                              CMETA_PARAM_BORROWED |
                              CMETA_PARAM_OWNED;
        check_false(cmeta_param_desc_valid(&pointer_param));

        pointer_param.flags = CMETA_PARAM_IN |
                              ((cmeta_param_flags)1u << 31);
        check_false(cmeta_param_desc_valid(&pointer_param));
    }

    it("rejects malformed function metadata and duplicate parameter names") {
        const cmeta_function_desc *valid =
            FunctionMeta(cmeta_function_test_sum);
        cmeta_function_desc fn = *valid;
        cmeta_param_desc duplicate[2] = {
            { sizeof(cmeta_param_desc), "same", &cmeta_type_int,
              CMETA_PARAM_IN, CMETA_ABI_SCALAR },
            { sizeof(cmeta_param_desc), "same", &cmeta_type_int,
              CMETA_PARAM_IN, CMETA_ABI_SCALAR }
        };

        check_true(cmeta_function_desc_valid(valid));

        fn.size = 0u;
        check_false(cmeta_function_desc_valid(&fn));
        fn = *valid;

        fn.params = NULL;
        check_false(cmeta_function_desc_valid(&fn));
        fn = *valid;

        fn.params = duplicate;
        check_false(cmeta_function_desc_valid(&fn));
        fn = *valid;

        fn.effects = CMETA_EFFECT_MAY_FAIL;
        fn.properties |= CMETA_PROP_TOTAL;
        check_false(cmeta_function_desc_valid(&fn));
    }
}
