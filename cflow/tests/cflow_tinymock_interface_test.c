#include <cflow/reactive.h>
#include <cflow/clock.h>
#include <cmeta/cmeta.h>

#include "tinytest.h"
#include "tinymock_cmeta.h"

TINYMOCk_INTERFACE(cflow_subscriber, CMETA_SUBSCRIBER_METHODS);
TINYMOCk_INTERFACE(cflow_clock, CMETA_CLOCK_METHODS);

suite("TinyMock existing CMeta interface") {
  it("consumes reflected Clock FunctionDesc without repeating signatures") {
    tinymock_cflow_clock mock;
    cflow_clock clock;
    cflow_instant scripted_now = {100u};
    bool scripted_advance = true;
    cflow_duration delta = cflow_duration_from_ns(25u);
    cflow_duration expected_delta = cflow_duration_from_ns(25u);

    tinymock_cflow_clock_init(&mock);
    clock = tinymock_cflow_clock_as_interface(&mock);

    check_true(TINYMOCk_INTERFACE_METHOD_SET_RETURN(
        cflow_clock, &mock, now, scripted_now));
    check_true(TINYMOCk_INTERFACE_METHOD_SET_RETURN(
        cflow_clock, &mock, advance, scripted_advance));

    check_equal(cflow_clock_now(&clock).ns, UINT64_C(100));
    check_true(cflow_clock_advance(&clock, delta));

    check_true(TINYMOCk_INTERFACE_METHOD_ARG_EQUAL_TYPED(
        cflow_clock, &mock, advance, 0u, "delta", expected_delta));
    check_true(cmeta_function_desc_equal(
        TINYMOCk_INTERFACE_METHOD_FUNCTION(cflow_clock, advance),
        cflow_clock_interface()->methods[1].function));
    check_equal(
        TINYMOCk_INTERFACE_METHOD_ABI(
            cflow_clock, advance)->return_carrier,
        (cmeta_abi_carrier)CMETA_ABI_SCALAR);

    tinymock_mock_verify_times(
        TINYMOCk_INTERFACE_METHOD(&mock, now), 1);
    tinymock_mock_verify_times(
        TINYMOCk_INTERFACE_METHOD(&mock, advance), 1);

    tinymock_cflow_clock_destroy(&mock);
  }

  it("mocks the public cflow_subscriber interface without a hand-written vtable") {
    tinymock_cflow_subscriber mock;
    cflow_subscriber subscriber;
    const cmeta_type_desc *type;
    const tinymock_recorded_call_t *call;
    int value = 41;

    tinymock_cflow_subscriber_init(&mock);
    subscriber = tinymock_cflow_subscriber_as_interface(&mock);
    type = cmeta_type_find("int");

    check_true(cflow_subscriber_valid(&subscriber));
    check_not_null(type);
    check_equal(cflow_subscriber_implementation(&subscriber),
                "tinymock:cflow_subscriber");

    tinymock_mock_set_default_return(
        TINYMOCk_INTERFACE_METHOD(&mock, value), TINYMOCk_RETURN(true));

    check_true(cflow_subscriber_value(&subscriber, type, &value));
    cflow_subscriber_error(&subscriber, "expected failure");
    cflow_subscriber_done(&subscriber);

    tinymock_mock_verify_times(TINYMOCk_INTERFACE_METHOD(&mock, value), 1);
    tinymock_mock_verify_times(TINYMOCk_INTERFACE_METHOD(&mock, error), 1);
    tinymock_mock_verify_times(TINYMOCk_INTERFACE_METHOD(&mock, done), 1);

    call = tinymock_mock_call_at(TINYMOCk_INTERFACE_METHOD(&mock, value), 0);
    check_not_null(call);
    check_equal(call->argc, (size_t)2);
    check(TINYMOCk_VALUE_AS(const cmeta_type_desc *, call->args[0]) == type);
    check(TINYMOCk_VALUE_AS(const void *, call->args[1]) == &value);

    call = tinymock_mock_call_at(TINYMOCk_INTERFACE_METHOD(&mock, error), 0);
    check_not_null(call);
    check_equal(TINYMOCk_VALUE_AS(const char *, call->args[0]),
                "expected failure");
  }
}
