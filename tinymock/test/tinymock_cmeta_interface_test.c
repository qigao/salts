#include <cmeta/interface.h>
#include "tinytest.h"
#include "tinymock_cmeta.h"

#define TINYMOCK_CMETA_COUNTER_METHODS(X, I) \
  X(I,R1,int,add,int,delta) \
  X(I,R0,int,value,_) \
  X(I,V1,void,reset_to,int,value)

CMETA_INTERFACE(tinymock_cmeta_counter, TINYMOCK_CMETA_COUNTER_METHODS);
TINYMOCk_INTERFACE(tinymock_cmeta_counter, TINYMOCK_CMETA_COUNTER_METHODS);

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
