#include "tinytest.h"
#include "tinymock_function.h"

#include "tinymock_function_consumer.h"
#include "tinymock_function_fixture.h"

TINYMOCk_FUNCTION_DECLARE(tinymock_fixture_add);
TINYMOCk_FUNCTION_DECLARE(tinymock_fixture_answer);

suite("TinyMock reflected free functions") {
  it("generates replacement definitions without repeating signatures") {
    const cmeta_function_desc *add_meta;
    const tinymock_recorded_call_t *call;

    TINYMOCk_FUNCTION_RESET(tinymock_fixture_add);
    TINYMOCk_FUNCTION_RESET(tinymock_fixture_answer);

    tinymock_mock_set_default_return(
        TINYMOCk_FUNCTION(tinymock_fixture_add),
        TINYMOCk_RETURN(17));
    tinymock_mock_set_default_return(
        TINYMOCk_FUNCTION(tinymock_fixture_answer),
        TINYMOCk_RETURN(25));

    check_equal(tinymock_function_consumer_run(9), 17);

    tinymock_mock_verify_times(
        TINYMOCk_FUNCTION(tinymock_fixture_add), 1);
    tinymock_mock_verify_times(
        TINYMOCk_FUNCTION(tinymock_fixture_answer), 1);

    call = tinymock_mock_call_at(
        TINYMOCk_FUNCTION(tinymock_fixture_add), 0);
    check_not_null(call);
    check_equal(call->argc, (size_t)2);
    check_equal(TINYMOCk_VALUE_AS(int, call->args[0]), 9);
    check_equal(TINYMOCk_VALUE_AS(int, call->args[1]), 25);

    add_meta = TINYMOCk_FUNCTION_META(tinymock_fixture_add);
    check_not_null(add_meta);
    check_true(cmeta_function_desc_valid(add_meta));
    check_equal(add_meta->name, "tinymock_fixture_add");
    check_equal(add_meta->param_count, (size_t)2);
    check_true(cmeta_type_equal(add_meta->return_type, &cmeta_type_int));
    check_true(cmeta_type_equal(
        cmeta_function_param(add_meta, 0)->type, &cmeta_type_int));
  }

  it("keeps stubbing independent from verification") {
    TINYMOCk_FUNCTION_RESET(tinymock_fixture_add);
    TINYMOCk_FUNCTION_RESET(tinymock_fixture_answer);

    tinymock_mock_set_default_return(
        TINYMOCk_FUNCTION(tinymock_fixture_add),
        TINYMOCk_RETURN(-3));
    tinymock_mock_set_default_return(
        TINYMOCk_FUNCTION(tinymock_fixture_answer),
        TINYMOCk_RETURN(4));

    check_equal(tinymock_function_consumer_run(1), -3);
    check_equal(tinymock_function_consumer_run(2), -3);

    tinymock_mock_verify_times(
        TINYMOCk_FUNCTION(tinymock_fixture_add), 2);
    tinymock_mock_verify_at_least(
        TINYMOCk_FUNCTION(tinymock_fixture_answer), 2);
    tinymock_mock_verify_at_most(
        TINYMOCk_FUNCTION(tinymock_fixture_answer), 2);
  }
}
