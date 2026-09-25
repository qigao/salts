#include <cmeta/invokable.h>
#include <cmeta/interface.h>
#include "tinytest.h"

typed_any(value, int, cmeta_invokable_increment, (int value)) {
    return value + 1;
}

typed_any(value, long, cmeta_invokable_add, (long left, long right)) {
    return left + right;
}

static const cmeta_param_desc increment_params[] = {
    {
        .size = sizeof(cmeta_param_desc),
        .name = "value",
        .type = &cmeta_type_int,
        .flags = CMETA_PARAM_IN
    }
};

static const cmeta_function_desc increment_function = {
    .size = sizeof(cmeta_function_desc),
    .name = "increment",
    .return_type = &cmeta_type_int,
    .params = increment_params,
    .param_count = 1u,
    .effects = CMETA_CONTRACT_EFFECTS(value),
    .properties = CMETA_CONTRACT_PROPERTIES(value)
};

static const cmeta_param_desc add_params[] = {
    {
        .size = sizeof(cmeta_param_desc),
        .name = "left",
        .type = &cmeta_type_long,
        .flags = CMETA_PARAM_IN
    },
    {
        .size = sizeof(cmeta_param_desc),
        .name = "right",
        .type = &cmeta_type_long,
        .flags = CMETA_PARAM_IN
    }
};

static const cmeta_data_desc *const increment_data_params[] = {
    &cmeta_data_int
};

static const cmeta_abi_carrier increment_abi_params[] = {
    CMETA_ABI_SCALAR
};

static const cmeta_function_abi_desc increment_abi = {
    .size = sizeof(cmeta_function_abi_desc),
    .function = &increment_function,
    .return_carrier = CMETA_ABI_SCALAR,
    .param_carriers = increment_abi_params,
    .param_count = 1u
};

static const cmeta_interface_method_desc increment_method = {
    .size = sizeof(cmeta_interface_method_desc),
    .name = "increment",
    .dispatch_arity = 1u,
    .flags = CMETA_INTERFACE_METHOD_NONE,
    .function = &increment_function,
    .abi = &increment_abi
};

static const cmeta_function_data_desc increment_data = {
    .size = sizeof(cmeta_function_data_desc),
    .function = &increment_function,
    .return_data = &cmeta_data_int,
    .params = increment_data_params,
    .param_count = 1u
};

const cmeta_data_desc *cmeta_invokable_peer_int_data(void);

static const cmeta_function_desc add_function = {
    .size = sizeof(cmeta_function_desc),
    .name = "add",
    .return_type = &cmeta_type_long,
    .params = add_params,
    .param_count = 2u,
    .effects = CMETA_CONTRACT_EFFECTS(value),
    .properties = CMETA_CONTRACT_PROPERTIES(value)
};

spec("CMeta invokable bridge") {
  it("validates semantic function data and binds it to execution") {
    cmeta_invokable invokable = CMETA_INVOKABLE_INIT;
    int input = 41;
    int output = 0;
    const void *args[] = {&input};

    check_true(cmeta_function_data_desc_valid(&increment_data));
    check_equal(cmeta_invokable_bind_data(
                    &increment_data, cmeta_invokable_increment, &invokable),
                CMETA_OK);
    check_true(invokable.data == &increment_data);
    check_equal(cmeta_invokable_invoke(&invokable, &output, args), CMETA_OK);
    check_equal(output, 42);
  }

  it("rejects semantic parameter/native type mismatch") {
    const cmeta_data_desc *wrong_params[] = {&cmeta_data_long};
    cmeta_function_data_desc wrong = increment_data;
    wrong.params = wrong_params;

    check_false(cmeta_function_data_desc_valid(&wrong));
  }

  it("rejects semantic return/native type mismatch") {
    cmeta_function_data_desc wrong = increment_data;
    wrong.return_data = &cmeta_data_long;

    check_false(cmeta_function_data_desc_valid(&wrong));
  }

  it("accepts equivalent semantic descriptor copies from another TU") {
    const cmeta_data_desc *peer = cmeta_invokable_peer_int_data();
    const cmeta_data_desc *params[] = {peer};
    cmeta_function_data_desc data = increment_data;

    check_not_null(peer);
    check_true(peer != &cmeta_data_int);
    check_true(cmeta_data_desc_equal(peer, &cmeta_data_int));
    data.return_data = peer;
    data.params = params;
    check_true(cmeta_function_data_desc_valid(&data));
  }


  it("binds and invokes a reflected unary callable") {
    cmeta_invokable invokable = CMETA_INVOKABLE_INIT;
    int input = 41;
    int output = 0;
    const void *args[] = {&input};

    check_equal(cmeta_invokable_bind(
                    &increment_function, cmeta_invokable_increment,
                    &invokable),
                CMETA_OK);
    check_true(cmeta_invokable_valid(&invokable));
    check_equal(cmeta_invokable_invoke(&invokable, &output, args), CMETA_OK);
    check_equal(output, 42);
  }

  it("binds and invokes a reflected binary callable") {
    cmeta_invokable invokable = CMETA_INVOKABLE_INIT;
    long left = 20;
    long right = 22;
    long output = 0;
    const void *args[] = {&left, &right};

    check_equal(cmeta_invokable_bind(
                    &add_function, cmeta_invokable_add, &invokable),
                CMETA_OK);
    check_equal(cmeta_invokable_invoke(&invokable, &output, args), CMETA_OK);
    check_equal(output, 42L);
  }

  it("rejects reflected signature mismatch") {
    cmeta_invokable invokable = CMETA_INVOKABLE_INIT;
    cmeta_param_desc wrong_param = increment_params[0];
    cmeta_function_desc wrong = increment_function;

    wrong_param.type = &cmeta_type_long;
    wrong.params = &wrong_param;
    check_equal(cmeta_invokable_bind(
                    &wrong, cmeta_invokable_increment, &invokable),
                CMETA_TYPE_MISMATCH);
  }

  it("rejects effect contract mismatch") {
    cmeta_invokable invokable = CMETA_INVOKABLE_INIT;
    cmeta_function_desc wrong = increment_function;

    wrong.effects = CMETA_EFFECT_IO;
    check_equal(cmeta_invokable_bind(
                    &wrong, cmeta_invokable_increment, &invokable),
                CMETA_TYPE_MISMATCH);
  }

  it("joins a fully reflected interface method to the same invokable") {
    cmeta_invokable invokable = CMETA_INVOKABLE_INIT;
    int input = 9;
    int output = 0;
    const void *args[] = {&input};

    check_true(cmeta_interface_method_reflection_valid(&increment_method));
    check_equal(cmeta_interface_method_invokable_bind(
                    &increment_method, &increment_data,
                    cmeta_invokable_increment, &invokable),
                CMETA_OK);
    check_equal(cmeta_invokable_invoke(&invokable, &output, args), CMETA_OK);
    check_equal(output, 10);
  }

}
