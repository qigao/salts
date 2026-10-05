#include "tinytest.h"

#define TINYMOCK_GENERATE_FUNCTION_OVERRIDES 1
#define TINYMOCK_SELECTIVE_FUNCTION_OVERRIDES 1
#define TINYMOCK_SELECTED_FUNCTION_tinymock_selective_mocked TINYMOCk_PP_PROBE_()
#define TINYMOCK_SELECTED_FUNCTION_tinymock_selective_void TINYMOCk_PP_PROBE_()
#define TINYMOCK_SELECTED_FUNCTION_tinymock_selective_extra TINYMOCk_PP_PROBE_()
#include "tinymock_function.h"
#include "tinymock_selective_fixture.h"
#include "tinymock_selective_extra_fixture.h"

int tinymock_selective_consume_mocked(int value);
int tinymock_selective_consume_real(int value);
void tinymock_selective_consume_void(void);
int tinymock_selective_consume_extra(int value);

TINYMOCk_FUNCTION_DECLARE(tinymock_selective_mocked);
TINYMOCk_FUNCTION_DECLARE(tinymock_selective_void);
TINYMOCk_FUNCTION_DECLARE(tinymock_selective_extra);

suite("TinyMock selective reflected overrides") {
  it("mocks only requested functions and leaves real symbols linkable") {
    int expected = 7;

    TINYMOCk_FUNCTION_RESET(tinymock_selective_mocked);
    TINYMOCk_FUNCTION_RESET(tinymock_selective_void);
    TINYMOCk_FUNCTION_RESET(tinymock_selective_extra);

    {
      int mocked_return = 55;
      int extra_return = 66;
      check_true(TINYMOCk_FUNCTION_SET_RETURN(
          tinymock_selective_mocked, mocked_return));
      check_true(TINYMOCk_FUNCTION_SET_RETURN(
          tinymock_selective_extra, extra_return));
    }

    check_equal(tinymock_selective_consume_mocked(7), 55);
    check_equal(tinymock_selective_consume_real(7), 107);
    check_equal(tinymock_selective_consume_extra(7), 66);
    tinymock_selective_consume_void();

    TINYMOCk_FUNCTION_VERIFY_TIMES(tinymock_selective_mocked, 1);
    TINYMOCk_FUNCTION_VERIFY_TIMES(tinymock_selective_void, 1);
    TINYMOCk_FUNCTION_VERIFY_TIMES(tinymock_selective_extra, 1);
    check_true(TINYMOCk_FUNCTION_ARG_EQUAL_TYPED(
        tinymock_selective_mocked, 0, "value", expected));

    TINYMOCk_FUNCTION_DESTROY(tinymock_selective_mocked);
    TINYMOCk_FUNCTION_DESTROY(tinymock_selective_void);
    TINYMOCk_FUNCTION_DESTROY(tinymock_selective_extra);
  }
}
