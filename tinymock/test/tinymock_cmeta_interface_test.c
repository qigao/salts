#include <cmeta/interface.h>
#include "tinytest.h"
#include "tinymock_cmeta.h"

#define TINYMOCK_CMETA_COUNTER_METHODS(X, I) \
  X(I,F1,int,add,value, \
    &cmeta_type_int,CMETA_ABI_SCALAR, \
    (int,delta,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)) \
  X(I,F0,int,value,value, \
    &cmeta_type_int,CMETA_ABI_SCALAR) \
  X(I,FV1,void,reset_to,stateful, \
    &cmeta_type_void,CMETA_ABI_VOID, \
    (int,value,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR))

CMETA_INTERFACE(tinymock_cmeta_counter, TINYMOCK_CMETA_COUNTER_METHODS);
TINYMOCk_INTERFACE(tinymock_cmeta_counter, TINYMOCK_CMETA_COUNTER_METHODS);

suite("TinyMock CMeta interface bridge") {
  it("generates a valid mock vtable from the CMeta method schema") {
    tinymock_tinymock_cmeta_counter mock;
    tinymock_cmeta_counter counter;

    tinymock_tinymock_cmeta_counter_init(&mock);
    counter = tinymock_tinymock_cmeta_counter_as_interface(&mock);

    {
      int zero = 0;
      check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, value, zero));
    }

    check_true(tinymock_cmeta_counter_valid(&counter));
    check_equal(tinymock_cmeta_counter_implementation(&counter),
                "tinymock:tinymock_cmeta_counter");
    check_equal(tinymock_cmeta_counter_value(&counter), 0);

    {
      const cmeta_interface_desc *meta = tinymock_cmeta_counter_interface();
      const cmeta_function_desc *value_fn =
          TINYMOCk_INTERFACE_METHOD_FUNCTION(tinymock_cmeta_counter, value);
      const cmeta_function_abi_desc *value_abi =
          TINYMOCk_INTERFACE_METHOD_ABI(tinymock_cmeta_counter, value);

      check_true(cmeta_interface_desc_valid(meta));
      check_true(cmeta_interface_method_reflection_valid(&meta->methods[1]));
      check_true(meta->methods[1].function == value_fn);
      check_true(meta->methods[1].abi == value_abi);
      check_equal(value_fn->name, "tinymock_cmeta_counter.value");
      check_equal(value_abi->return_carrier,
                  (cmeta_abi_carrier)CMETA_ABI_SCALAR);
    }

    tinymock_mock_verify_times(TINYMOCk_INTERFACE_METHOD(&mock, value), 1);
    tinymock_mock_verify_never(TINYMOCk_INTERFACE_METHOD(&mock, add));
    tinymock_mock_verify_never(TINYMOCk_INTERFACE_METHOD(&mock, reset_to));
    tinymock_tinymock_cmeta_counter_destroy(&mock);
  }

  it("stubs returns and records typed method arguments") {
    tinymock_tinymock_cmeta_counter mock;
    tinymock_cmeta_counter counter;

    tinymock_tinymock_cmeta_counter_init(&mock);
    counter = tinymock_tinymock_cmeta_counter_as_interface(&mock);

    {
      int add_return = 17;
      int value_return = 23;
      check_true(TINYMOCk_INTERFACE_SET_RETURN(
          &mock, add, add_return));
      check_true(TINYMOCk_INTERFACE_SET_RETURN(
          &mock, value, value_return));
    }

    check_equal(tinymock_cmeta_counter_add(&counter, 5), 17);
    check_equal(tinymock_cmeta_counter_value(&counter), 23);
    tinymock_cmeta_counter_reset_to(&counter, 9);

    tinymock_mock_verify_times(TINYMOCk_INTERFACE_METHOD(&mock, add), 1);
    tinymock_mock_verify_times(TINYMOCk_INTERFACE_METHOD(&mock, value), 1);
    tinymock_mock_verify_times(TINYMOCk_INTERFACE_METHOD(&mock, reset_to), 1);
    tinymock_mock_verify_at_least(TINYMOCk_INTERFACE_METHOD(&mock, add), 1);
    tinymock_mock_verify_at_most(TINYMOCk_INTERFACE_METHOD(&mock, add), 1);

    {
      int expected_add = 5;
      int expected_reset = 9;
      check_true(TINYMOCk_INTERFACE_ARG_EQUAL_TYPED(
          &mock, add, 0u, "delta", expected_add));
      check_true(TINYMOCk_INTERFACE_ARG_EQUAL_TYPED(
          &mock, reset_to, 0u, "value", expected_reset));
    }
    tinymock_tinymock_cmeta_counter_destroy(&mock);
  }
}
