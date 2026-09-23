#include <cmeta/interface.h>
#include "tinytest.h"
#include "tinymock_cmeta.h"

#define TINYMOCK_CMETA_COUNTER_METHODS(X, I) \
  X(I,R1,int,add,int,delta) \
  X(I,R0,int,value,_) \
  X(I,V1,void,reset_to,int,value)

CMETA_INTERFACE(tinymock_cmeta_counter, TINYMOCK_CMETA_COUNTER_METHODS);
TINYMOCk_INTERFACE(tinymock_cmeta_counter, TINYMOCK_CMETA_COUNTER_METHODS);

#define TINYMOCK_CMETA_REFLECTED_METHODS(X, I) \
  X(I,F1,int,add,value,&cmeta_type_int,CMETA_ABI_SCALAR, \
    (int, delta, CMETA_PARAM_IN, &cmeta_type_int, CMETA_ABI_SCALAR)) \
  X(I,F0,int,value,value,&cmeta_type_int,CMETA_ABI_SCALAR) \
  X(I,F1,void,reset_to,stateful,&cmeta_type_void,CMETA_ABI_VOID, \
    (int, value, CMETA_PARAM_IN, &cmeta_type_int, CMETA_ABI_SCALAR))

CMETA_INTERFACE(tinymock_cmeta_reflected,
                TINYMOCK_CMETA_REFLECTED_METHODS);
TINYMOCk_INTERFACE(tinymock_cmeta_reflected,
                   TINYMOCK_CMETA_REFLECTED_METHODS);

suite("TinyMock CMeta interface bridge") {
  it("generates a valid mock vtable from the CMeta method schema") {
    tinymock_tinymock_cmeta_counter mock;
    tinymock_cmeta_counter counter;

    tinymock_tinymock_cmeta_counter_init(&mock);
    counter = tinymock_tinymock_cmeta_counter_as_interface(&mock);

    check_true(tinymock_cmeta_counter_valid(&counter));
    check_equal(tinymock_cmeta_counter_implementation(&counter),
                "tinymock:tinymock_cmeta_counter");
    check_equal(tinymock_cmeta_counter_value(&counter), 0);

    tinymock_mock_verify_times(TINYMOCk_INTERFACE_METHOD(&mock, value), 1);
    tinymock_mock_verify_never(TINYMOCk_INTERFACE_METHOD(&mock, add));
    tinymock_mock_verify_never(TINYMOCk_INTERFACE_METHOD(&mock, reset_to));
  }

  it("consumes canonical FunctionDesc for reflected methods") {
    tinymock_tinymock_cmeta_reflected mock;
    tinymock_cmeta_reflected dependency;
    tinymock_cmeta_captor captor;
    int add_return = 17;
    int value_return = 23;
    int expected_delta = 5;
    int expected_reset = 9;

    tinymock_tinymock_cmeta_reflected_init(&mock);
    dependency =
        tinymock_tinymock_cmeta_reflected_as_interface(&mock);

    check_true(cmeta_interface_method_reflection_valid(
        &tinymock_cmeta_reflected_interface()->methods[0]));
    check_true(cmeta_function_desc_equal(
        TINYMOCk_INTERFACE_METHOD_FUNCTION(
            tinymock_cmeta_reflected, add),
        tinymock_cmeta_reflected_interface()->methods[0].function));
    check_true(cmeta_function_abi_desc_equal(
        TINYMOCk_INTERFACE_METHOD_ABI(
            tinymock_cmeta_reflected, add),
        tinymock_cmeta_reflected_interface()->methods[0].abi));

    check_true(TINYMOCk_INTERFACE_METHOD_SET_RETURN(
        tinymock_cmeta_reflected, &mock, add, add_return));
    check_true(TINYMOCk_INTERFACE_METHOD_SET_RETURN(
        tinymock_cmeta_reflected, &mock, value, value_return));

    check_equal(tinymock_cmeta_reflected_add(&dependency, 5), 17);
    check_equal(tinymock_cmeta_reflected_value(&dependency), 23);
    tinymock_cmeta_reflected_reset_to(&dependency, 9);

    tinymock_mock_verify_times(
        TINYMOCk_INTERFACE_METHOD(&mock, add), 1);
    tinymock_mock_verify_times(
        TINYMOCk_INTERFACE_METHOD(&mock, reset_to), 1);

    check_true(TINYMOCk_INTERFACE_METHOD_ARG_EQUAL_TYPED(
        tinymock_cmeta_reflected, &mock, add, 0u,
        "delta", expected_delta));
    check_true(TINYMOCk_INTERFACE_METHOD_ARG_EQUAL_TYPED(
        tinymock_cmeta_reflected, &mock, reset_to, 0u,
        "value", expected_reset));

    tinymock_cmeta_captor_init(&captor);
    check_true(TINYMOCk_INTERFACE_METHOD_CAPTURE(
        tinymock_cmeta_reflected, &mock, add, 0u,
        "delta", &captor));
    check_equal(*(const int *)tinymock_cmeta_captor_value(&captor), 5);
    tinymock_cmeta_captor_destroy(&captor);

    tinymock_tinymock_cmeta_reflected_destroy(&mock);
  }

  it("stubs returns and records typed method arguments") {
    tinymock_tinymock_cmeta_counter mock;
    tinymock_cmeta_counter counter;
    const tinymock_recorded_call_t *call;

    tinymock_tinymock_cmeta_counter_init(&mock);
    counter = tinymock_tinymock_cmeta_counter_as_interface(&mock);

    tinymock_mock_set_default_return(
        TINYMOCk_INTERFACE_METHOD(&mock, add), TINYMOCk_RETURN(17));
    tinymock_mock_set_default_return(
        TINYMOCk_INTERFACE_METHOD(&mock, value), TINYMOCk_RETURN(23));

    check_equal(tinymock_cmeta_counter_add(&counter, 5), 17);
    check_equal(tinymock_cmeta_counter_value(&counter), 23);
    tinymock_cmeta_counter_reset_to(&counter, 9);

    tinymock_mock_verify_times(TINYMOCk_INTERFACE_METHOD(&mock, add), 1);
    tinymock_mock_verify_times(TINYMOCk_INTERFACE_METHOD(&mock, value), 1);
    tinymock_mock_verify_times(TINYMOCk_INTERFACE_METHOD(&mock, reset_to), 1);
    tinymock_mock_verify_at_least(TINYMOCk_INTERFACE_METHOD(&mock, add), 1);
    tinymock_mock_verify_at_most(TINYMOCk_INTERFACE_METHOD(&mock, add), 1);

    call = tinymock_mock_call_at(TINYMOCk_INTERFACE_METHOD(&mock, add), 0);
    check_not_null(call);
    check_equal(call->argc, (size_t)1);
    check_equal(TINYMOCk_VALUE_AS(int, call->args[0]), 5);

    call = tinymock_mock_call_at(TINYMOCk_INTERFACE_METHOD(&mock, reset_to), 0);
    check_not_null(call);
    check_equal(call->argc, (size_t)1);
    check_equal(TINYMOCk_VALUE_AS(int, call->args[0]), 9);
  }
}
