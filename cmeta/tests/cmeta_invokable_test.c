#include <cmeta/invokable.h>
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
}
