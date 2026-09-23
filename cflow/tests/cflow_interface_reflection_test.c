#include <cflow/clock.h>

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
}
