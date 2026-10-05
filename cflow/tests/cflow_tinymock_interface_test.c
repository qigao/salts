#include <cflow/reactive.h>
#include <cmeta/cmeta.h>

#include "tinytest.h"
#include "tinymock_cmeta.h"

TINYMOCk_INTERFACE(cflow_subscriber, CMETA_SUBSCRIBER_METHODS);

TINYMOCk_INTERFACE(cflow_waitable, CMETA_WAITABLE_METHODS);

static void cflow_tinymock_test_wake(void *user) {
    (void)user;
}

suite("TinyMock existing CMeta interface") {
  it("mocks the fully reflected cflow_subscriber contract") {
    tinymock_cflow_subscriber mock;
    cflow_subscriber subscriber;
    const cmeta_type_desc *type;
    const char *message = "expected failure";
    int value = 41;
    bool accept = true;

    tinymock_cflow_subscriber_init(&mock);
    subscriber = tinymock_cflow_subscriber_as_interface(&mock);
    type = cmeta_type_find("int");

    check_true(cflow_subscriber_valid(&subscriber));
    check_not_null(type);
    check_equal(cflow_subscriber_implementation(&subscriber),
                "tinymock:cflow_subscriber");

    {
      const cmeta_interface_desc *meta = cflow_subscriber_interface();
      const cmeta_function_desc *value_fn =
          TINYMOCk_INTERFACE_METHOD_FUNCTION(cflow_subscriber, value);
      const cmeta_function_desc *error_fn =
          TINYMOCk_INTERFACE_METHOD_FUNCTION(cflow_subscriber, error);

      check_true(cmeta_interface_desc_valid(meta));
      check_true(cmeta_interface_method_reflection_valid(&meta->methods[0]));
      check_true(cmeta_interface_method_reflection_valid(&meta->methods[1]));
      check_true(cmeta_interface_method_reflection_valid(&meta->methods[2]));
      check_true(meta->methods[0].function == value_fn);
      check_true(meta->methods[1].function == error_fn);
      check_equal(value_fn->result_flags,
                  (cmeta_result_flags)CMETA_RESULT_VALUE);
      check_equal(value_fn->param_count, (size_t)2);
      check_true((value_fn->params[0].flags &
                  (CMETA_PARAM_IN | CMETA_PARAM_BORROWED)) ==
                 (CMETA_PARAM_IN | CMETA_PARAM_BORROWED));
      check_true((value_fn->params[1].flags &
                  (CMETA_PARAM_IN | CMETA_PARAM_BORROWED)) ==
                 (CMETA_PARAM_IN | CMETA_PARAM_BORROWED));
      check_true(value_fn->params[0].type == &cmeta_type_descriptor_ptr);
      check_true(value_fn->params[1].type == &cmeta_type_void_ptr);
      check_true(error_fn->params[0].type == &cmeta_type_char_ptr);
    }

    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, value, accept));

    check_true(cflow_subscriber_value(&subscriber, type, &value));
    cflow_subscriber_error(&subscriber, message);
    cflow_subscriber_done(&subscriber);

    TINYMOCk_INTERFACE_VERIFY_TIMES(&mock, value, 1);
    TINYMOCk_INTERFACE_VERIFY_TIMES(&mock, error, 1);
    TINYMOCk_INTERFACE_VERIFY_TIMES(&mock, done, 1);

    check_true(tinymock_cmeta_history_arg_pointer_equal_name(
        TINYMOCk_INTERFACE_METHOD_HISTORY(&mock, value),
        0u, "type", (const void *)type));
    check_true(tinymock_cmeta_history_arg_pointer_equal_name(
        TINYMOCk_INTERFACE_METHOD_HISTORY(&mock, value),
        0u, "value", (const void *)&value));
    check_true(tinymock_cmeta_history_arg_pointer_equal_name(
        TINYMOCk_INTERFACE_METHOD_HISTORY(&mock, error),
        0u, "message", (const void *)message));

    tinymock_cflow_subscriber_destroy(&mock);
  }

  it("mocks the fully reflected waitable contract") {
    tinymock_cflow_waitable mock;
    cflow_waitable waitable;
    cflow_waker expected = {cflow_tinymock_test_wake, &mock};
    bool armed = true;

    tinymock_cflow_waitable_init(&mock);
    waitable = tinymock_cflow_waitable_as_interface(&mock);

    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, arm, armed));
    check_true(cflow_waitable_arm(&waitable, expected));
    cflow_waitable_cancel(&waitable);

    TINYMOCk_INTERFACE_VERIFY_TIMES(&mock, arm, 1);
    TINYMOCk_INTERFACE_VERIFY_TIMES(&mock, cancel, 1);
    check_true(TINYMOCk_INTERFACE_ARG_EQUAL_TYPED(
        &mock, arm, 0u, "waker", expected));

    tinymock_cflow_waitable_destroy(&mock);
  }
}
