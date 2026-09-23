#include "reflected_dependency.h"
#include "tinytest.h"
#include "tinymock_function.h"

TINYMOCk_USE(tinymock_reflected_add);
TINYMOCk_USE(tinymock_reflected_status);

suite("TinyMock reflected free functions") {
  before_each() {
    mock_tinymock_reflected_add_reset();
    mock_tinymock_reflected_status_reset();
  }

  it("replays FunctionDecl without repeating the signature") {
    const cmeta_function_desc *mock_meta =
        TINYMOCk_FUNCTION_META(tinymock_reflected_add);
    const cmeta_function_desc *local_meta =
        FunctionMeta(tinymock_reflected_add);
    const tinymock_recorded_call_t *call;

    check_true(cmeta_function_desc_valid(mock_meta));
    check_true(cmeta_function_desc_valid(local_meta));
    check_equal(mock_meta->name, "tinymock_reflected_add");
    check_equal(mock_meta->param_count, (size_t)2);
    check_true(cmeta_type_equal(mock_meta->return_type, local_meta->return_type));
    check_true(cmeta_type_equal(mock_meta->params[0].type,
                                local_meta->params[0].type));
    check_equal(mock_meta->params[0].flags,
                (cmeta_param_flags)CMETA_PARAM_IN);

    tinymock_mock_set_default_return(
        TINYMOCk_FUNCTION(tinymock_reflected_add), TINYMOCk_RETURN(7));

    check_equal(tinymock_reflected_add(3, 4), 7);
    tinymock_mock_verify_times(
        TINYMOCk_FUNCTION(tinymock_reflected_add), 1);

    call = tinymock_mock_call_at(
        TINYMOCk_FUNCTION(tinymock_reflected_add), 0);
    check_not_null(call);
    check_equal(call->argc, (size_t)2);
    check_equal(TINYMOCk_VALUE_AS(int, call->args[0]), 3);
    check_equal(TINYMOCk_VALUE_AS(int, call->args[1]), 4);
  }

  it("supports reflected zero-parameter functions") {
    tinymock_mock_set_default_return(
        TINYMOCk_FUNCTION(tinymock_reflected_status), TINYMOCk_RETURN(42));

    check_equal(tinymock_reflected_status(), 42);
    tinymock_mock_verify_times(
        TINYMOCk_FUNCTION(tinymock_reflected_status), 1);
    check_equal(
        TINYMOCk_FUNCTION_META(tinymock_reflected_status)->param_count,
        (size_t)0);
  }
}
