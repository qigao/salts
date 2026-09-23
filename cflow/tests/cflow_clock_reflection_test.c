#include <cflow/clock.h>
#include "tinytest.h"

suite("CFlow Clock reflected interface") {
  it("publishes canonical function and ABI semantics") {
    const cmeta_interface_desc *meta = cflow_clock_interface();
    const cmeta_function_desc *now;
    const cmeta_function_abi_desc *now_abi;
    const cmeta_function_desc *advance;
    const cmeta_function_abi_desc *advance_abi;
    const cmeta_param_desc *delta;
    const cmeta_function_desc *destroy;

    check_true(cmeta_interface_desc_valid(meta));
    check_equal(meta->name, "cflow_clock");
    check_equal(meta->method_count, (size_t)3);

    check_true(cmeta_interface_method_reflection_valid(&meta->methods[0]));
    now = cmeta_interface_method_function(&meta->methods[0]);
    now_abi = cmeta_interface_method_abi(&meta->methods[0]);
    check_equal(now->name, "cflow_clock.now");
    check_true(cmeta_type_equal(
        now->return_type, &cmeta_type_cflow_instant));
    check_equal(now->effects, (cmeta_effects)CMETA_EFFECT_STATEFUL);
    check_equal(now_abi->return_carrier,
                (cmeta_abi_carrier)CMETA_ABI_AGGREGATE);

    advance = cmeta_interface_method_function(&meta->methods[1]);
    advance_abi = cmeta_interface_method_abi(&meta->methods[1]);
    check_equal(advance->name, "cflow_clock.advance");
    check_true(cmeta_type_equal(advance->return_type, &cmeta_type_bool));
    check_equal(advance->effects, (cmeta_effects)CMETA_EFFECT_STATEFUL);
    check_equal(advance->param_count, (size_t)1);
    delta = cmeta_function_param(advance, 0u);
    check_not_null(delta);
    check_equal(delta->name, "delta");
    check_true(cmeta_type_equal(
        delta->type, &cmeta_type_cflow_duration));
    check_equal(delta->flags, (cmeta_param_flags)CMETA_PARAM_IN);
    check_equal(cmeta_function_param_abi(advance_abi, 0u),
                (cmeta_abi_carrier)CMETA_ABI_AGGREGATE);

    destroy = cmeta_interface_method_function(&meta->methods[2]);
    check_equal(destroy->name, "cflow_clock.destroy");
    check_true(cmeta_type_equal(destroy->return_type, &cmeta_type_void));
    check_equal(
        cmeta_interface_method_abi(&meta->methods[2])->return_carrier,
        (cmeta_abi_carrier)CMETA_ABI_VOID);
    check_false(cmeta_interface_method_is_owning(&meta->methods[2]));
  }

  it("preserves the existing exact dispatch ABI") {
    cflow_clock clock = {0};

    check_true(cflow_clock_virtual_init(
        &clock, (cflow_instant){100u}));
    check_equal(cflow_clock_now(&clock).ns, UINT64_C(100));
    check_true(cflow_clock_advance(
        &clock, cflow_duration_from_ns(25u)));
    check_equal(cflow_clock_now(&clock).ns, UINT64_C(125));
    cflow_clock_destroy(&clock);
  }
}
