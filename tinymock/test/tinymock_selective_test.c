#include "tinytest.h"
#include "tinymock_function.h"

int tinymock_selective_consume_mocked(int value);
int tinymock_selective_consume_real(int value);
void tinymock_selective_consume_void(void);

TINYMOCk_FUNCTION_DECLARE(tinymock_selective_mocked);
TINYMOCk_FUNCTION_DECLARE(tinymock_selective_void);

suite("TinyMock selective reflected overrides") {
  it("mocks only requested functions and leaves real symbols linkable") {
    int expected = 7;

    TINYMOCk_FUNCTION_RESET(tinymock_selective_mocked);
    TINYMOCk_FUNCTION_RESET(tinymock_selective_void);

    tinymock_mock_set_default_return(
        TINYMOCk_FUNCTION(tinymock_selective_mocked),
        TINYMOCk_RETURN(55));

    check_equal(tinymock_selective_consume_mocked(7), 55);
    check_equal(tinymock_selective_consume_real(7), 107);
    tinymock_selective_consume_void();

    tinymock_mock_verify_times(
        TINYMOCk_FUNCTION(tinymock_selective_mocked), 1);
    tinymock_mock_verify_times(
        TINYMOCk_FUNCTION(tinymock_selective_void), 1);
    check_true(TINYMOCk_FUNCTION_ARG_EQUAL(
        tinymock_selective_mocked, 0, "value", expected));

    TINYMOCk_FUNCTION_DESTROY(tinymock_selective_mocked);
    TINYMOCk_FUNCTION_DESTROY(tinymock_selective_void);
  }
}
