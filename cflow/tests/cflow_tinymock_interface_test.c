#include <cflow/reactive.h>
#include <cmeta/cmeta.h>

#include "tinytest.h"
#include "tinymock_cmeta.h"

TINYMOCk_INTERFACE(cflow_subscriber, CMETA_SUBSCRIBER_METHODS);

TINYMOCk_INTERFACE(cflow_waitable, CMETA_WAITABLE_METHODS);
TINYMOCk_INTERFACE(cflow_publisher, CFLOW_PUBLISHER_METHODS);
TINYMOCk_INTERFACE(cflow_executor, CMETA_EXECUTOR_METHODS);
TINYMOCk_INTERFACE(cflow_executor_control, CMETA_EXECUTOR_CONTROL_METHODS);
TINYMOCk_INTERFACE(cflow_scheduler, CMETA_SCHEDULER_METHODS);

static void cflow_tinymock_test_wake(void *user) {
    (void)user;
}

static void cflow_tinymock_test_task(void *user) {
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
  it("mocks the fully reflected publisher contract") {
    tinymock_cflow_publisher mock;
    cflow_publisher publisher;
    const char *scripted_name = "mock-publisher";
    const cmeta_type_desc *scripted_type = &cmeta_type_int;
    cflow_step scripted_step = {CFLOW_STEP_VALUE, {0}, NULL};
    cflow_publisher_terminal scripted_terminal = CFLOW_PUBLISHER_ERROR;
    const char *scripted_error = "terminal failure";
    cflow_publish_context ctx = {0};
    int out_value = 0;
    cflow_waker waker = {cflow_tinymock_test_wake, &mock};

    tinymock_cflow_publisher_init(&mock);
    publisher = tinymock_cflow_publisher_as_interface(&mock);

    {
      const cmeta_interface_desc *meta = cflow_publisher_interface();
      const cmeta_function_desc *name_fn =
          TINYMOCk_INTERFACE_METHOD_FUNCTION(cflow_publisher, name);
      const cmeta_function_desc *resume_fn =
          TINYMOCk_INTERFACE_METHOD_FUNCTION(cflow_publisher, resume);
      const cmeta_function_desc *poll_fn =
          TINYMOCk_INTERFACE_METHOD_FUNCTION(cflow_publisher, poll_terminal);
      size_t method_index;

      check_true(cmeta_interface_desc_valid(meta));
      check_equal(meta->method_count, (size_t)7);
      for (method_index = 0u; method_index < meta->method_count;
           ++method_index)
        check_true(cmeta_interface_method_reflection_valid(
            &meta->methods[method_index]));

      check_equal(name_fn->result_flags,
                  (cmeta_result_flags)CMETA_RESULT_BORROWED);
      check_equal(resume_fn->result_flags,
                  (cmeta_result_flags)CMETA_RESULT_VALUE);
      check_true((resume_fn->params[0].flags &
                  (CMETA_PARAM_IN | CMETA_PARAM_BORROWED |
                   CMETA_PARAM_NULLABLE)) ==
                 (CMETA_PARAM_IN | CMETA_PARAM_BORROWED |
                  CMETA_PARAM_NULLABLE));
      check_true((resume_fn->params[1].flags &
                  (CMETA_PARAM_OUT | CMETA_PARAM_BORROWED)) ==
                 (CMETA_PARAM_OUT | CMETA_PARAM_BORROWED));
      check_true(resume_fn->params[0].type ==
                 &cflow_type_publish_context_ptr);
      check_true(resume_fn->params[1].type == &cmeta_type_void_ptr);

      check_equal(poll_fn->result_flags,
                  (cmeta_result_flags)CMETA_RESULT_VALUE);
      check_true((poll_fn->params[0].flags &
                  (CMETA_PARAM_OUT | CMETA_PARAM_BORROWED |
                   CMETA_PARAM_NULLABLE)) ==
                 (CMETA_PARAM_OUT | CMETA_PARAM_BORROWED |
                  CMETA_PARAM_NULLABLE));
      check_true(poll_fn->params[0].type == &cmeta_type_char_ptr_ptr);
      check_true(cmeta_interface_method_owns_self(&meta->methods[4]));
    }

    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, name, scripted_name));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(
        &mock, output_type, scripted_type));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, resume, scripted_step));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(
        &mock, poll_terminal, scripted_terminal));
    check_true(TINYMOCk_INTERFACE_SET_OUT(
        &mock, poll_terminal, "error", scripted_error));

    check_equal(cflow_publisher_name(&publisher), scripted_name);
    check_true(cflow_publisher_output_type(&publisher) == scripted_type);
    check_true(cflow_publisher_resume(&publisher, &ctx, &out_value).kind ==
               CFLOW_STEP_VALUE);
    cflow_publisher_cancel(&publisher);
    cflow_publisher_bind_terminal_waker(&publisher, waker);

    {
      const char *error = NULL;
      check_true(cflow_publisher_poll_terminal(&publisher, &error) ==
                 CFLOW_PUBLISHER_ERROR);
      check_equal(error, scripted_error);
    }

    TINYMOCk_INTERFACE_VERIFY_TIMES(&mock, name, 1);
    TINYMOCk_INTERFACE_VERIFY_TIMES(&mock, output_type, 1);
    TINYMOCk_INTERFACE_VERIFY_TIMES(&mock, resume, 1);
    TINYMOCk_INTERFACE_VERIFY_TIMES(&mock, cancel, 1);
    TINYMOCk_INTERFACE_VERIFY_TIMES(&mock, bind_terminal_waker, 1);
    TINYMOCk_INTERFACE_VERIFY_TIMES(&mock, poll_terminal, 1);
    TINYMOCk_INTERFACE_VERIFY_NEVER(&mock, destroy);

    check_true(tinymock_cmeta_history_arg_pointer_equal_name(
        TINYMOCk_INTERFACE_METHOD_HISTORY(&mock, resume),
        0u, "ctx", (const void *)&ctx));
    check_true(tinymock_cmeta_history_arg_pointer_equal_name(
        TINYMOCk_INTERFACE_METHOD_HISTORY(&mock, resume),
        0u, "out_value", (const void *)&out_value));
    check_true(TINYMOCk_INTERFACE_ARG_EQUAL_TYPED(
        &mock, bind_terminal_waker, 0u, "waker", waker));

    cflow_publisher_destroy(&publisher);
    check_false(cflow_publisher_valid(&publisher));
    TINYMOCk_INTERFACE_VERIFY_TIMES(&mock, destroy, 1);

    tinymock_cflow_publisher_destroy(&mock);
  }

  it("mocks the fully reflected executor contract") {
    tinymock_cflow_executor mock;
    cflow_executor executor;
    cflow_admission_status try_status = CFLOW_ADMISSION_ACCEPTED;
    bool yes = true;
    size_t ready = 2u;
    size_t pending = 3u;
    cflow_executor_stats stats = {
      .capacity = 8u, .pending = 3u, .peak_pending = 4u
    };
    cflow_executor_stats observed = {0};
    int user_value = 17;
    cflow_task_fn expected_task = cflow_tinymock_test_task;

    tinymock_cflow_executor_init(&mock);
    executor = tinymock_cflow_executor_as_interface(&mock);

    check_true(TINYMOCk_INTERFACE_SET_RETURN(
        &mock, try_post, try_status));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, post, yes));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, run_one, yes));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, run_ready, ready));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, wait_idle, yes));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, pending, pending));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, shutdown, yes));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, get_stats, yes));
    check_true(TINYMOCk_INTERFACE_SET_OUT(
        &mock, get_stats, "out", stats));

    check_true(cflow_executor_try_post(
        &executor, cflow_tinymock_test_task, &user_value) ==
        CFLOW_ADMISSION_ACCEPTED);
    check_true(cflow_executor_post(
        &executor, cflow_tinymock_test_task, &user_value));
    check_true(cflow_executor_run_one(&executor));
    check_equal(cflow_executor_run_ready(&executor), (size_t)2);
    check_true(cflow_executor_wait_idle(&executor));
    check_equal(cflow_executor_pending(&executor), (size_t)3);
    check_true(cflow_executor_shutdown(&executor));
    check_true(cflow_executor_get_stats(&executor, &observed));
    check_equal(observed.capacity, (size_t)8);
    check_equal(observed.pending, (size_t)3);

    check_true(TINYMOCk_INTERFACE_ARG_EQUAL_TYPED(
        &mock, try_post, 0u, "fn", expected_task));
    check_true(tinymock_cmeta_history_arg_pointer_equal_name(
        TINYMOCk_INTERFACE_METHOD_HISTORY(&mock, try_post),
        0u, "user", &user_value));

    {
      const cmeta_interface_desc *meta = cflow_executor_interface();
      check_true(cmeta_interface_desc_valid(meta));
      check_equal(meta->method_count, (size_t)9);
      check_true(cmeta_interface_method_owns_self(&meta->methods[8]));
      check_equal(
          cmeta_interface_method_function(&meta->methods[0])->result_flags,
          (cmeta_result_flags)CMETA_RESULT_VALUE);
    }

    cflow_executor_destroy(&executor);
    check_false(cflow_executor_valid(&executor));
    TINYMOCk_INTERFACE_VERIFY_TIMES(&mock, destroy, 1);
    tinymock_cflow_executor_destroy(&mock);
  }

  it("mocks the fully reflected executor control contract") {
    tinymock_cflow_executor_control mock;
    cflow_executor_control control;
    cflow_executor_post_status post_status = CFLOW_EXECUTOR_POST_ACCEPTED;
    cflow_executor_wait_status wait_status = CFLOW_EXECUTOR_WAIT_IDLE;
    bool yes = true;
    cflow_executor_protocol_stats stats = {
      .capacity = 16u, .accepted = 4u, .completed = 4u
    };
    cflow_executor_protocol_stats observed = {0};
    cflow_executor_shutdown_policy policy = CFLOW_EXECUTOR_SHUTDOWN_DRAIN;
    int user_value = 23;

    tinymock_cflow_executor_control_init(&mock);
    control = tinymock_cflow_executor_control_as_interface(&mock);

    check_true(TINYMOCk_INTERFACE_SET_RETURN(
        &mock, post, post_status));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(
        &mock, wait_idle, wait_status));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, shutdown, yes));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, get_stats, yes));
    check_true(TINYMOCk_INTERFACE_SET_OUT(
        &mock, get_stats, "out", stats));

    check_true(cflow_executor_control_post(
        &control, cflow_tinymock_test_task, &user_value) ==
        CFLOW_EXECUTOR_POST_ACCEPTED);
    check_true(cflow_executor_control_wait_idle(&control) ==
               CFLOW_EXECUTOR_WAIT_IDLE);
    check_true(cflow_executor_control_shutdown(&control, policy));
    check_true(cflow_executor_control_get_stats(&control, &observed));
    check_equal(observed.capacity, (size_t)16);
    check_equal(observed.accepted, (size_t)4);

    check_true(tinymock_cmeta_history_arg_pointer_equal_name(
        TINYMOCk_INTERFACE_METHOD_HISTORY(&mock, post),
        0u, "user", &user_value));

    tinymock_cflow_executor_control_destroy(&mock);
  }

  it("mocks the fully reflected scheduler contract") {
    tinymock_cflow_scheduler mock;
    cflow_scheduler scheduler;
    cflow_schedule_result scheduled = {
      CFLOW_ADMISSION_ACCEPTED, (cflow_task_id)42u
    };
    cflow_task_id posted = 43u;
    bool yes = true;
    size_t ready = 2u;
    size_t advanced = 3u;
    size_t idle_runs = 4u;
    uint64_t now = 99u;
    size_t pending = 5u;
    cflow_scheduler_stats stats = {
      .ready_capacity = 8u,
      .timer_capacity = 16u,
      .ready_pending = 1u,
      .timer_pending = 2u
    };
    cflow_scheduler_stats observed = {0};
    cflow_task_fn expected_task = cflow_tinymock_test_task;
    uint64_t delay = 7u;
    uint64_t ticks = 11u;
    size_t max_steps = 13u;
    cflow_task_id cancel_id = 42u;
    int user_value = 29;

    tinymock_cflow_scheduler_init(&mock);
    scheduler = tinymock_cflow_scheduler_as_interface(&mock);

    check_true(TINYMOCk_INTERFACE_SET_RETURN(
        &mock, try_post_after, scheduled));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, post_after, posted));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, cancel, yes));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, run_one, yes));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, run_ready, ready));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, advance, advanced));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(
        &mock, run_until_idle, idle_runs));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, wait_idle, yes));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, now, now));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, pending, pending));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, shutdown, yes));
    check_true(TINYMOCk_INTERFACE_SET_RETURN(&mock, get_stats, yes));
    check_true(TINYMOCk_INTERFACE_SET_OUT(
        &mock, get_stats, "out", stats));

    check_true(cflow_scheduler_try_post_after(
        &scheduler, delay, cflow_tinymock_test_task, &user_value).task_id ==
        scheduled.task_id);
    check_equal(cflow_scheduler_post_after(
        &scheduler, delay, cflow_tinymock_test_task, &user_value),
        posted);
    check_true(cflow_scheduler_cancel(&scheduler, cancel_id));
    check_true(cflow_scheduler_run_one(&scheduler));
    check_equal(cflow_scheduler_run_ready(&scheduler), ready);
    check_equal(cflow_scheduler_advance(&scheduler, ticks), advanced);
    check_equal(cflow_scheduler_run_until_idle(&scheduler, max_steps),
                idle_runs);
    check_true(cflow_scheduler_wait_idle(&scheduler));
    check_equal(cflow_scheduler_now(&scheduler), now);
    check_equal(cflow_scheduler_pending(&scheduler), pending);
    check_true(cflow_scheduler_shutdown(&scheduler));
    check_true(cflow_scheduler_get_stats(&scheduler, &observed));
    check_equal(observed.ready_capacity, (size_t)8);
    check_equal(observed.timer_capacity, (size_t)16);

    check_true(TINYMOCk_INTERFACE_ARG_EQUAL_TYPED(
        &mock, try_post_after, 0u, "fn", expected_task));
    check_true(tinymock_cmeta_history_arg_pointer_equal_name(
        TINYMOCk_INTERFACE_METHOD_HISTORY(&mock, try_post_after),
        0u, "user", &user_value));

    {
      const cmeta_interface_desc *meta = cflow_scheduler_interface();
      const cmeta_function_desc *try_fn =
          TINYMOCk_INTERFACE_METHOD_FUNCTION(
              cflow_scheduler, try_post_after);
      const cmeta_function_desc *stats_fn =
          TINYMOCk_INTERFACE_METHOD_FUNCTION(cflow_scheduler, get_stats);
      size_t method_index;

      check_true(cmeta_interface_desc_valid(meta));
      check_equal(meta->method_count, (size_t)13);
      for (method_index = 0u; method_index < meta->method_count;
           ++method_index)
        check_true(cmeta_interface_method_reflection_valid(
            &meta->methods[method_index]));
      check_equal(try_fn->result_flags,
                  (cmeta_result_flags)CMETA_RESULT_VALUE);
      check_true((try_fn->params[2].flags &
                  (CMETA_PARAM_IN | CMETA_PARAM_BORROWED |
                   CMETA_PARAM_NULLABLE)) ==
                 (CMETA_PARAM_IN | CMETA_PARAM_BORROWED |
                  CMETA_PARAM_NULLABLE));
      check_true(try_fn->params[1].type == &cflow_type_task_fn);
      check_true(stats_fn->params[0].type ==
                 &cflow_type_scheduler_stats_ptr);
      check_true(cmeta_interface_method_owns_self(&meta->methods[12]));
    }

    cflow_scheduler_destroy(&scheduler);
    check_false(cflow_scheduler_valid(&scheduler));
    TINYMOCk_INTERFACE_VERIFY_TIMES(&mock, destroy, 1);
    tinymock_cflow_scheduler_destroy(&mock);
  }

}
