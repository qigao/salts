#include <cflow/clock.h>

#include "tinytest.h"
#include "tinymock_cmeta.h"

TINYMOCk_INTERFACE(cflow_clock, CMETA_CLOCK_METHODS);

suite("TinyMock reflected CFlow clock interface") {
  it("uses typed history and typed returns from canonical method reflection") {
    tinymock_cflow_clock mock;
    cflow_clock clock;
    cflow_instant scripted_now = { UINT64_C(1234) };
    cflow_duration delta = { UINT64_C(25) };
    cflow_duration expected = { UINT64_C(25) };
    cflow_instant now;
    tinymock_cmeta_captor captor;

    tinymock_cflow_clock_init(&mock);
    clock = tinymock_cflow_clock_as_interface(&mock);

    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, now, scripted_now));
    tinymock_mock_set_default_return(
        TINYMOCk_INTERFACE_METHOD(&mock, advance),
        TINYMOCk_RETURN(true));

    now = cflow_clock_now(&clock);
    check_equal(now.ns, UINT64_C(1234));
    check_true(cflow_clock_advance(&clock, delta));

    tinymock_mock_verify_times(TINYMOCk_INTERFACE_METHOD(&mock, now), 1);
    tinymock_mock_verify_times(TINYMOCk_INTERFACE_METHOD(&mock, advance), 1);

    check_true(TINYMOCk_INTERFACE_ARG_EQUAL_TYPED(
        &mock, advance, 0u, "delta", expected));

    tinymock_cmeta_captor_init(&captor);
    check_true(TINYMOCk_INTERFACE_CAPTURE(
        &mock, advance, 0u, "delta", &captor));
    check_true(cmeta_type_equal(
        tinymock_cmeta_captor_type(&captor), &cflow_type_duration));
    check_equal(
        ((const cflow_duration *)tinymock_cmeta_captor_value(&captor))->ns,
        UINT64_C(25));
    tinymock_cmeta_captor_destroy(&captor);

    check_true(cmeta_function_desc_equal(
        TINYMOCk_INTERFACE_METHOD_FUNCTION(cflow_clock, advance),
        cflow_clock_interface()->methods[1].function));
    check_true(cmeta_function_abi_desc_equal(
        TINYMOCk_INTERFACE_METHOD_ABI(cflow_clock, advance),
        cflow_clock_interface()->methods[1].abi));

    tinymock_cflow_clock_destroy(&mock);
  }
}
