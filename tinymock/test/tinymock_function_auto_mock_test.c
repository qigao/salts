#include "tinytest.h"
#include "tinymock_function.h"

#include "tinymock_function_consumer.h"
#include "tinymock_function_fixture.h"

static int tinymock_test_callback_a(int value) {
  return value + 3;
}

static int tinymock_test_callback_b(int value) {
  return value + 9;
}

TINYMOCk_FUNCTION_DECLARE(tinymock_fixture_add);
TINYMOCk_FUNCTION_DECLARE(tinymock_fixture_answer);
TINYMOCk_FUNCTION_DECLARE(tinymock_fixture_pointer);
TINYMOCk_FUNCTION_DECLARE(tinymock_fixture_write_size);
TINYMOCk_FUNCTION_DECLARE(tinymock_fixture_adjust_int);
TINYMOCk_FUNCTION_DECLARE(tinymock_fixture_unknown_ptr);
TINYMOCk_FUNCTION_DECLARE(tinymock_fixture_nullable_out);
TINYMOCk_FUNCTION_DECLARE(tinymock_fixture_notify);
TINYMOCk_FUNCTION_DECLARE(tinymock_fixture_shutdown);
TINYMOCk_FUNCTION_DECLARE(tinymock_fixture_box_copy);
TINYMOCk_FUNCTION_DECLARE(tinymock_fixture_pointer_answer);
TINYMOCk_FUNCTION_DECLARE(tinymock_fixture_apply_callback);
TINYMOCk_FUNCTION_DECLARE(tinymock_fixture_callback_answer);
TINYMOCk_FUNCTION_DECLARE(tinymock_fixture_mode_echo);

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

    {
      int expected_left = 9;
      int expected_right = 25;
      tinymock_cmeta_captor captor;

      check_true(TINYMOCk_FUNCTION_ARG_EQUAL(
          tinymock_fixture_add, 0, "left", expected_left));
      check_equal(TINYMOCk_FUNCTION_COUNT_EQUAL(
          tinymock_fixture_add, "right", expected_right), (size_t)1);

      tinymock_cmeta_captor_init(&captor);
      check_true(TINYMOCk_FUNCTION_CAPTURE(
          tinymock_fixture_add, 0, "right", &captor));
      check_true(cmeta_type_equal(
          tinymock_cmeta_captor_type(&captor), &cmeta_type_int));
      check_equal(*(const int *)tinymock_cmeta_captor_value(&captor), 25);
      check_equal(tinymock_cmeta_captor_count(&captor), (size_t)1);
      tinymock_cmeta_captor_destroy(&captor);
    }

    add_meta = TINYMOCk_FUNCTION_META(tinymock_fixture_add);
    check_not_null(add_meta);
    check_true(cmeta_function_desc_valid(add_meta));
    check_equal(add_meta->name, "tinymock_fixture_add");
    check_equal(add_meta->param_count, (size_t)2);
    check_true(cmeta_type_equal(add_meta->return_type, &cmeta_type_int));
    check_true(cmeta_type_equal(
        cmeta_function_param(add_meta, 0)->type, &cmeta_type_int));

    TINYMOCk_FUNCTION_DESTROY(tinymock_fixture_add);
    TINYMOCk_FUNCTION_DESTROY(tinymock_fixture_answer);
  }

  it("matches reflected pointer arguments by identity") {
    int value = 7;
    int *expected = &value;

    TINYMOCk_FUNCTION_RESET(tinymock_fixture_pointer);
    tinymock_mock_set_default_return(
        TINYMOCk_FUNCTION(tinymock_fixture_pointer),
        TINYMOCk_RETURN(33));

    check_equal(tinymock_function_consumer_pointer(&value), 33);
    check_true(TINYMOCk_FUNCTION_ARG_EQUAL(
        tinymock_fixture_pointer, 0, "value", expected));

    {
      int other = 7;
      int *different = &other;
      check_false(TINYMOCk_FUNCTION_ARG_EQUAL(
          tinymock_fixture_pointer, 0, "value", different));
    }

    TINYMOCk_FUNCTION_DESTROY(tinymock_fixture_pointer);
  }

  it("scripts reflected OUT and INOUT parameters") {
    size_t written = 0u;
    size_t scripted_written = 64u;
    int adjusted = 5;
    int scripted_adjusted = 41;

    TINYMOCk_FUNCTION_RESET(tinymock_fixture_write_size);
    tinymock_mock_set_default_return(
        TINYMOCk_FUNCTION(tinymock_fixture_write_size),
        TINYMOCk_RETURN(7));
    check_true(TINYMOCk_FUNCTION_SET_OUT(
        tinymock_fixture_write_size, "written", scripted_written));

    check_equal(
        tinymock_function_consumer_write_size(9, &written), 7);
    check_equal(written, (size_t)64);
    tinymock_mock_verify_times(
        TINYMOCk_FUNCTION(tinymock_fixture_write_size), 1);

    TINYMOCk_FUNCTION_RESET(tinymock_fixture_adjust_int);
    tinymock_mock_set_default_return(
        TINYMOCk_FUNCTION(tinymock_fixture_adjust_int),
        TINYMOCk_RETURN(8));
    check_true(TINYMOCk_FUNCTION_SET_OUT(
        tinymock_fixture_adjust_int, "value", scripted_adjusted));

    check_equal(tinymock_function_consumer_adjust_int(&adjusted), 8);
    check_equal(adjusted, 41);

    TINYMOCk_FUNCTION_DESTROY(tinymock_fixture_write_size);
    TINYMOCk_FUNCTION_DESTROY(tinymock_fixture_adjust_int);
  }

  it("rejects unsafe output metadata and handles nullable null") {
    int input = 5;
    int scripted = 9;
    size_t nullable_value = 22u;

    TINYMOCk_FUNCTION_RESET(tinymock_fixture_pointer);
    check_false(TINYMOCk_FUNCTION_SET_OUT(
        tinymock_fixture_pointer, "value", scripted));
    TINYMOCk_FUNCTION_DESTROY(tinymock_fixture_pointer);

    TINYMOCk_FUNCTION_RESET(tinymock_fixture_unknown_ptr);
    check_false(TINYMOCk_FUNCTION_SET_OUT(
        tinymock_fixture_unknown_ptr, "value", scripted));
    TINYMOCk_FUNCTION_DESTROY(tinymock_fixture_unknown_ptr);

    TINYMOCk_FUNCTION_RESET(tinymock_fixture_nullable_out);
    tinymock_mock_set_default_return(
        TINYMOCk_FUNCTION(tinymock_fixture_nullable_out),
        TINYMOCk_RETURN(3));
    check_true(TINYMOCk_FUNCTION_SET_OUT(
        tinymock_fixture_nullable_out, "written", nullable_value));

    check_equal(tinymock_function_consumer_nullable_out(NULL), 3);
    tinymock_mock_verify_times(
        TINYMOCk_FUNCTION(tinymock_fixture_nullable_out), 1);

    check_equal(input, 5);
    TINYMOCk_FUNCTION_DESTROY(tinymock_fixture_nullable_out);
  }

  it("auto-mocks parameterized and zero-argument void functions") {
    size_t written = 0u;
    size_t scripted_written = 77u;
    int expected_event = 9;
    tinymock_cmeta_captor captor;

    TINYMOCk_FUNCTION_RESET(tinymock_fixture_notify);
    TINYMOCk_FUNCTION_RESET(tinymock_fixture_shutdown);

    check_true(TINYMOCk_FUNCTION_SET_OUT(
        tinymock_fixture_notify, "written", scripted_written));

    tinymock_function_consumer_notify(9, &written);
    tinymock_function_consumer_shutdown();

    check_equal(written, (size_t)77);
    tinymock_mock_verify_times(
        TINYMOCk_FUNCTION(tinymock_fixture_notify), 1);
    tinymock_mock_verify_times(
        TINYMOCk_FUNCTION(tinymock_fixture_shutdown), 1);

    check_true(TINYMOCk_FUNCTION_ARG_EQUAL(
        tinymock_fixture_notify, 0, "event", expected_event));

    tinymock_cmeta_captor_init(&captor);
    check_true(TINYMOCk_FUNCTION_CAPTURE(
        tinymock_fixture_notify, 0, "event", &captor));
    check_true(cmeta_type_equal(
        tinymock_cmeta_captor_type(&captor), &cmeta_type_int));
    check_equal(*(const int *)tinymock_cmeta_captor_value(&captor), 9);
    tinymock_cmeta_captor_destroy(&captor);

    check_true(cmeta_type_equal(
        TINYMOCk_FUNCTION_META(tinymock_fixture_notify)->return_type,
        &cmeta_type_void));
    check_true(cmeta_type_equal(
        TINYMOCk_FUNCTION_META(tinymock_fixture_shutdown)->return_type,
        &cmeta_type_void));

    TINYMOCk_FUNCTION_DESTROY(tinymock_fixture_notify);
    TINYMOCk_FUNCTION_DESTROY(tinymock_fixture_shutdown);
  }

  it("records, matches, captures, and returns aggregate values") {
    tinymock_fixture_box input = {7};
    tinymock_fixture_box expected_input = {7};
    tinymock_fixture_box scripted = {41};
    tinymock_fixture_box result;
    tinymock_cmeta_captor captor;

    TINYMOCk_FUNCTION_RESET(tinymock_fixture_box_copy);
    check_true(TINYMOCk_FUNCTION_SET_RETURN(
        tinymock_fixture_box_copy, scripted));

    result = tinymock_function_consumer_box(input);
    check_equal(result.value, 41);

    tinymock_mock_verify_times(
        TINYMOCk_FUNCTION(tinymock_fixture_box_copy), 1);
    check_true(TINYMOCk_FUNCTION_ARG_EQUAL_TYPED(
        tinymock_fixture_box_copy, 0, "input", expected_input));

    tinymock_cmeta_captor_init(&captor);
    check_true(TINYMOCk_FUNCTION_CAPTURE(
        tinymock_fixture_box_copy, 0, "input", &captor));
    check_true(cmeta_type_equal(
        tinymock_cmeta_captor_type(&captor),
        &tinymock_fixture_box_type));
    check_equal(
        ((const tinymock_fixture_box *)
            tinymock_cmeta_captor_value(&captor))->value,
        7);

    input.value = 99;
    check_equal(
        ((const tinymock_fixture_box *)
            tinymock_cmeta_captor_value(&captor))->value,
        7);

    tinymock_cmeta_captor_destroy(&captor);
    TINYMOCk_FUNCTION_DESTROY(tinymock_fixture_box_copy);
  }

  it("lets typed scalar and pointer returns override legacy defaults") {
    int typed_add = 88;
    int pointer_value = 123;
    int *typed_pointer = &pointer_value;

    TINYMOCk_FUNCTION_RESET(tinymock_fixture_add);
    tinymock_mock_set_default_return(
        TINYMOCk_FUNCTION(tinymock_fixture_add),
        TINYMOCk_RETURN(17));

    check_true(TINYMOCk_FUNCTION_SET_RETURN(
        tinymock_fixture_add, typed_add));
    check_equal(tinymock_function_consumer_run(1), 88);

    TINYMOCk_FUNCTION_CLEAR_RETURN(tinymock_fixture_add);
    check_equal(tinymock_function_consumer_run(1), 17);

    TINYMOCk_FUNCTION_RESET(tinymock_fixture_pointer_answer);
    check_true(TINYMOCk_FUNCTION_SET_RETURN(
        tinymock_fixture_pointer_answer, typed_pointer));
    check_true(tinymock_function_consumer_pointer_answer() == &pointer_value);

    TINYMOCk_FUNCTION_DESTROY(tinymock_fixture_add);
    TINYMOCk_FUNCTION_DESTROY(tinymock_fixture_pointer_answer);
  }

  it("records, captures, and returns typed function-pointer values") {
    tinymock_fixture_callback expected_callback;
    tinymock_fixture_callback scripted_callback;
    tinymock_fixture_callback returned_callback;
    tinymock_cmeta_captor captor;

    expected_callback = tinymock_test_callback_a;
    scripted_callback = tinymock_test_callback_b;

    TINYMOCk_FUNCTION_RESET(tinymock_fixture_apply_callback);
    tinymock_mock_set_default_return(
        TINYMOCk_FUNCTION(tinymock_fixture_apply_callback),
        TINYMOCk_RETURN(77));

    check_equal(
        tinymock_function_consumer_apply_callback(tinymock_test_callback_a, 4),
        77);
    tinymock_mock_verify_times(
        TINYMOCk_FUNCTION(tinymock_fixture_apply_callback), 1);

    check_true(TINYMOCk_FUNCTION_ARG_EQUAL_TYPED(
        tinymock_fixture_apply_callback, 0, "callback",
        expected_callback));

    expected_callback = tinymock_test_callback_b;
    check_false(TINYMOCk_FUNCTION_ARG_EQUAL_TYPED(
        tinymock_fixture_apply_callback, 0, "callback",
        expected_callback));

    tinymock_cmeta_captor_init(&captor);
    check_true(TINYMOCk_FUNCTION_CAPTURE(
        tinymock_fixture_apply_callback, 0, "callback", &captor));
    check_true(cmeta_type_equal(
        tinymock_cmeta_captor_type(&captor),
        &tinymock_fixture_callback_type));
    check_true(
        *(const tinymock_fixture_callback *)
            tinymock_cmeta_captor_value(&captor) == tinymock_test_callback_a);
    tinymock_cmeta_captor_destroy(&captor);

    TINYMOCk_FUNCTION_RESET(tinymock_fixture_callback_answer);
    check_true(TINYMOCk_FUNCTION_SET_RETURN(
        tinymock_fixture_callback_answer, scripted_callback));

    returned_callback = tinymock_function_consumer_callback_answer();
    check_true(returned_callback == tinymock_test_callback_b);
    check_equal(returned_callback(5), 14);

    TINYMOCk_FUNCTION_DESTROY(tinymock_fixture_apply_callback);
    TINYMOCk_FUNCTION_DESTROY(tinymock_fixture_callback_answer);
  }

  it("records, captures, and returns typed enum values") {
    tinymock_fixture_mode input = TINYMOCK_FIXTURE_MODE_READY;
    tinymock_fixture_mode expected = TINYMOCK_FIXTURE_MODE_READY;
    tinymock_fixture_mode scripted = TINYMOCK_FIXTURE_MODE_DONE;
    tinymock_fixture_mode result;
    tinymock_cmeta_captor captor;

    TINYMOCk_FUNCTION_RESET(tinymock_fixture_mode_echo);
    check_true(TINYMOCk_FUNCTION_SET_RETURN(
        tinymock_fixture_mode_echo, scripted));

    result = tinymock_function_consumer_mode(input);
    check_equal((int)result, (int)TINYMOCK_FIXTURE_MODE_DONE);

    tinymock_mock_verify_times(
        TINYMOCk_FUNCTION(tinymock_fixture_mode_echo), 1);

    check_true(TINYMOCk_FUNCTION_ARG_EQUAL_TYPED(
        tinymock_fixture_mode_echo, 0, "input", expected));

    expected = TINYMOCK_FIXTURE_MODE_IDLE;
    check_false(TINYMOCk_FUNCTION_ARG_EQUAL_TYPED(
        tinymock_fixture_mode_echo, 0, "input", expected));

    tinymock_cmeta_captor_init(&captor);
    check_true(TINYMOCk_FUNCTION_CAPTURE(
        tinymock_fixture_mode_echo, 0, "input", &captor));
    check_true(cmeta_type_equal(
        tinymock_cmeta_captor_type(&captor),
        &tinymock_fixture_mode_type));
    check_equal(
        (int)*(const tinymock_fixture_mode *)
            tinymock_cmeta_captor_value(&captor),
        (int)TINYMOCK_FIXTURE_MODE_READY);
    tinymock_cmeta_captor_destroy(&captor);

    check_equal(
        TINYMOCk_FUNCTION_ABI(tinymock_fixture_mode_echo)->return_carrier,
        (cmeta_abi_carrier)CMETA_ABI_ENUM);
    check_equal(
        cmeta_function_param_abi(
            TINYMOCk_FUNCTION_ABI(tinymock_fixture_mode_echo), 0u),
        (cmeta_abi_carrier)CMETA_ABI_ENUM);

    TINYMOCk_FUNCTION_DESTROY(tinymock_fixture_mode_echo);
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

    TINYMOCk_FUNCTION_DESTROY(tinymock_fixture_add);
    TINYMOCk_FUNCTION_DESTROY(tinymock_fixture_answer);
  }
}
