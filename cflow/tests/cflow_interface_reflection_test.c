#include <cflow/clock.h>
#include <cflow/reactive.h>

#include "tinytest.h"

suite("CFlow clock interface reflection") {
  it("publishes complete canonical method semantics") {
    const cmeta_interface_desc *meta = cflow_clock_interface();
    const cmeta_interface_method_desc *now;
    const cmeta_interface_method_desc *advance;
    const cmeta_interface_method_desc *destroy;
    const cmeta_function_desc *fn;
    const cmeta_function_abi_desc *abi;
    const cmeta_param_desc *delta;

    check_true(cmeta_interface_desc_valid(meta));
    check_equal(meta->name, "cflow_clock");
    check_equal(meta->method_count, (size_t)3);

    now = &meta->methods[0];
    advance = &meta->methods[1];
    destroy = &meta->methods[2];

    check_true(cmeta_interface_method_reflection_valid(now));
    fn = cmeta_interface_method_function(now);
    abi = cmeta_interface_method_abi(now);
    check_equal(fn->name, "cflow_clock.now");
    check_true(cmeta_type_equal(fn->return_type, &cflow_type_instant));
    check_equal(fn->effects, (cmeta_effects)CMETA_EFFECT_STATEFUL);
    check_equal(abi->return_carrier,
                (cmeta_abi_carrier)CMETA_ABI_AGGREGATE);
    check_equal(fn->param_count, (size_t)0);

    check_true(cmeta_interface_method_reflection_valid(advance));
    fn = cmeta_interface_method_function(advance);
    abi = cmeta_interface_method_abi(advance);
    check_equal(fn->name, "cflow_clock.advance");
    check_true(cmeta_type_equal(fn->return_type, &cmeta_type_bool));
    check_equal(fn->effects, (cmeta_effects)CMETA_EFFECT_STATEFUL);
    check_equal(abi->return_carrier,
                (cmeta_abi_carrier)CMETA_ABI_SCALAR);
    check_equal(fn->param_count, (size_t)1);

    delta = cmeta_function_param(fn, 0u);
    check_not_null(delta);
    check_equal(delta->name, "delta");
    check_equal(delta->flags, (cmeta_param_flags)CMETA_PARAM_IN);
    check_true(cmeta_type_equal(delta->type, &cflow_type_duration));
    check_equal(cmeta_function_param_abi(abi, 0u),
                (cmeta_abi_carrier)CMETA_ABI_AGGREGATE);

    check_true(cmeta_interface_method_reflection_valid(destroy));
    fn = cmeta_interface_method_function(destroy);
    abi = cmeta_interface_method_abi(destroy);
    check_equal(fn->name, "cflow_clock.destroy");
    check_true(cmeta_type_equal(fn->return_type, &cmeta_type_void));
    check_equal(fn->effects, (cmeta_effects)CMETA_EFFECT_STATEFUL);
    check_equal(abi->return_carrier,
                (cmeta_abi_carrier)CMETA_ABI_VOID);
    check_equal(destroy->flags,
                (cmeta_interface_method_flags)CMETA_INTERFACE_METHOD_NONE);
  }

  it("keeps time structs semantically distinct from scalar integers") {
    check_true(cmeta_type_desc_valid(&cflow_type_duration));
    check_true(cmeta_type_desc_valid(&cflow_type_instant));
    check_false(cmeta_type_equal(&cflow_type_duration, &cflow_type_instant));
    check_equal(cflow_type_duration.kind, CMETA_T_OBJECT);
    check_equal(cflow_type_instant.kind, CMETA_T_OBJECT);
  }

  it("publishes complete waitable semantics") {
    const cmeta_interface_desc *meta = cflow_waitable_interface();
    const cmeta_function_desc *arm_fn;
    const cmeta_function_abi_desc *arm_abi;
    const cmeta_param_desc *waker;

    check_true(cmeta_type_desc_valid(&cflow_type_waker));
    check_true(cmeta_interface_desc_valid(meta));
    check_equal(meta->method_count, (size_t)2);
    check_true(cmeta_interface_method_reflection_valid(&meta->methods[0]));
    check_true(cmeta_interface_method_reflection_valid(&meta->methods[1]));

    arm_fn = cmeta_interface_method_function(&meta->methods[0]);
    arm_abi = cmeta_interface_method_abi(&meta->methods[0]);
    check_equal(arm_fn->name, "cflow_waitable.arm");
    check_equal(arm_fn->effects, (cmeta_effects)CMETA_EFFECT_STATEFUL);
    check_equal(arm_fn->result_flags,
                (cmeta_result_flags)CMETA_RESULT_VALUE);
    check_true(cmeta_type_equal(arm_fn->return_type, &cmeta_type_bool));
    check_equal(arm_abi->return_carrier,
                (cmeta_abi_carrier)CMETA_ABI_SCALAR);

    waker = cmeta_function_param(arm_fn, 0u);
    check_not_null(waker);
    check_equal(waker->name, "waker");
    check_equal(waker->flags, (cmeta_param_flags)CMETA_PARAM_IN);
    check_true(cmeta_type_equal(waker->type, &cflow_type_waker));
    check_equal(cmeta_function_param_abi(arm_abi, 0u),
                (cmeta_abi_carrier)CMETA_ABI_AGGREGATE);

    check_equal(cmeta_interface_method_function(&meta->methods[1])->name,
                "cflow_waitable.cancel");
  }
  it("publishes complete executor semantics") {
    const cmeta_interface_desc *meta = cflow_executor_interface();
    const cmeta_function_desc *try_fn;
    const cmeta_function_desc *stats_fn;
    size_t index;

    check_true(cmeta_interface_desc_valid(meta));
    check_equal(meta->method_count, (size_t)9);
    for (index = 0u; index < meta->method_count; ++index)
      check_true(cmeta_interface_method_reflection_valid(&meta->methods[index]));

    try_fn = cmeta_interface_method_function(&meta->methods[0]);
    check_equal(try_fn->name, "cflow_executor.try_post");
    check_equal(try_fn->result_flags,
                (cmeta_result_flags)CMETA_RESULT_VALUE);
    check_true(try_fn->return_type == &cflow_type_admission_status);
    check_equal(try_fn->param_count, (size_t)2);
    check_true(try_fn->params[0].type == &cflow_type_task_fn);
    check_equal(cmeta_interface_method_abi(&meta->methods[0])
                    ->param_carriers[0],
                (cmeta_abi_carrier)CMETA_ABI_FUNCTION_POINTER);
    check_true((try_fn->params[1].flags &
                (CMETA_PARAM_IN | CMETA_PARAM_BORROWED |
                 CMETA_PARAM_NULLABLE)) ==
               (CMETA_PARAM_IN | CMETA_PARAM_BORROWED |
                CMETA_PARAM_NULLABLE));
    check_true(try_fn->params[1].type == &cmeta_type_void_ptr);

    stats_fn = cmeta_interface_method_function(&meta->methods[7]);
    check_true(stats_fn->params[0].type == &cflow_type_executor_stats_ptr);
    check_equal(stats_fn->params[0].flags,
                (cmeta_param_flags)(CMETA_PARAM_OUT | CMETA_PARAM_BORROWED));

    check_true(cmeta_interface_method_owns_self(&meta->methods[8]));
    check_equal(cmeta_interface_method_function(&meta->methods[8])->name,
                "cflow_executor.destroy");
  }

  it("publishes complete executor control semantics") {
    const cmeta_interface_desc *meta = cflow_executor_control_interface();
    const cmeta_function_desc *post_fn;
    const cmeta_function_desc *shutdown_fn;
    const cmeta_function_desc *stats_fn;
    size_t index;

    check_true(cmeta_interface_desc_valid(meta));
    check_equal(meta->method_count, (size_t)4);
    for (index = 0u; index < meta->method_count; ++index)
      check_true(cmeta_interface_method_reflection_valid(&meta->methods[index]));

    post_fn = cmeta_interface_method_function(&meta->methods[0]);
    check_true(post_fn->return_type == &cflow_type_executor_post_status);
    check_equal(post_fn->result_flags,
                (cmeta_result_flags)CMETA_RESULT_VALUE);
    check_true(post_fn->params[0].type == &cflow_type_task_fn);
    check_true((post_fn->params[1].flags &
                (CMETA_PARAM_IN | CMETA_PARAM_BORROWED |
                 CMETA_PARAM_NULLABLE)) ==
               (CMETA_PARAM_IN | CMETA_PARAM_BORROWED |
                CMETA_PARAM_NULLABLE));

    check_true(cmeta_interface_method_function(&meta->methods[1])->return_type ==
               &cflow_type_executor_wait_status);

    shutdown_fn = cmeta_interface_method_function(&meta->methods[2]);
    check_true(shutdown_fn->params[0].type ==
               &cflow_type_executor_shutdown_policy);
    check_equal(shutdown_fn->params[0].flags,
                (cmeta_param_flags)CMETA_PARAM_IN);

    stats_fn = cmeta_interface_method_function(&meta->methods[3]);
    check_true(stats_fn->params[0].type ==
               &cflow_type_executor_protocol_stats_ptr);
    check_equal(stats_fn->params[0].flags,
                (cmeta_param_flags)(CMETA_PARAM_OUT | CMETA_PARAM_BORROWED));

    check_false(cmeta_interface_desc_has_owning_method(meta));
  }

  it("publishes complete scheduler semantics") {
    const cmeta_interface_desc *meta = cflow_scheduler_interface();
    const cmeta_function_desc *try_fn;
    const cmeta_function_abi_desc *try_abi;
    const cmeta_function_desc *stats_fn;
    size_t index;

    check_true(cmeta_type_desc_valid(&cflow_type_scheduler_stats));
    check_true(cmeta_type_desc_valid(&cflow_type_scheduler_stats_ptr));
    check_true(cmeta_interface_desc_valid(meta));
    check_equal(meta->method_count, (size_t)13);

    for (index = 0u; index < meta->method_count; ++index)
      check_true(cmeta_interface_method_reflection_valid(&meta->methods[index]));

    try_fn = cmeta_interface_method_function(&meta->methods[0]);
    try_abi = cmeta_interface_method_abi(&meta->methods[0]);
    check_equal(try_fn->name, "cflow_scheduler.try_post_after");
    check_true(try_fn->return_type == &cflow_type_schedule_result);
    check_equal(try_fn->result_flags,
                (cmeta_result_flags)CMETA_RESULT_VALUE);
    check_equal(try_abi->return_carrier,
                (cmeta_abi_carrier)CMETA_ABI_AGGREGATE);
    check_equal(try_fn->param_count, (size_t)3);
    check_true(try_fn->params[0].type == &cmeta_type_uint64);
    check_equal(try_fn->params[0].flags,
                (cmeta_param_flags)CMETA_PARAM_IN);
    check_true(try_fn->params[1].type == &cflow_type_task_fn);
    check_equal(try_abi->param_carriers[1],
                (cmeta_abi_carrier)CMETA_ABI_FUNCTION_POINTER);
    check_true(try_fn->params[2].type == &cmeta_type_void_ptr);
    check_true((try_fn->params[2].flags &
                (CMETA_PARAM_IN | CMETA_PARAM_BORROWED |
                 CMETA_PARAM_NULLABLE)) ==
               (CMETA_PARAM_IN | CMETA_PARAM_BORROWED |
                CMETA_PARAM_NULLABLE));

    check_true(cmeta_interface_method_function(&meta->methods[1])->return_type ==
               &cflow_type_task_id);
    check_true(cmeta_interface_method_function(&meta->methods[2])
                   ->params[0].type == &cflow_type_task_id);
    check_true(cmeta_interface_method_function(&meta->methods[5])
                   ->params[0].type == &cmeta_type_uint64);
    check_true(cmeta_interface_method_function(&meta->methods[6])
                   ->params[0].type == &cmeta_type_size);

    stats_fn = cmeta_interface_method_function(&meta->methods[11]);
    check_true(stats_fn->params[0].type == &cflow_type_scheduler_stats_ptr);
    check_equal(stats_fn->params[0].flags,
                (cmeta_param_flags)(CMETA_PARAM_OUT | CMETA_PARAM_BORROWED));

    check_true(cmeta_interface_method_owns_self(&meta->methods[12]));
    check_equal(cmeta_interface_method_function(&meta->methods[12])->name,
                "cflow_scheduler.destroy");
  }

}
