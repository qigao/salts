#include "cmeta_interface_function_fixture.h"
#include "tinytest.h"

const cmeta_interface_desc *cmeta_interface_function_peer_a(void);
const cmeta_interface_desc *cmeta_interface_function_peer_b(void);

static void check_reflected_counter(const cmeta_interface_desc *meta) {
    const cmeta_interface_method_desc *add;
    const cmeta_function_desc *fn;
    const cmeta_function_abi_desc *abi;
    const cmeta_param_desc *delta;

    check_true(cmeta_interface_desc_valid(meta));
    check_equal(meta->name, "cmeta_reflected_counter");
    check_equal(meta->method_count, (size_t)3);

    add = &meta->methods[0];
    check_true(cmeta_interface_method_reflection_valid(add));
    check_equal(add->name, "add");
    check_equal(cmeta_interface_method_arity(add), (size_t)1);

    fn = cmeta_interface_method_function(add);
    abi = cmeta_interface_method_abi(add);
    check_not_null(fn);
    check_not_null(abi);
    check_equal(fn->name, "cmeta_reflected_counter.add");
    check_true(cmeta_type_equal(fn->return_type, &cmeta_type_int));
    check_equal(fn->effects, (cmeta_effects)CMETA_EFFECT_PURE);
    check_true(cmeta_properties_include(
        fn->properties,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS));
    check_equal(abi->return_carrier,
                (cmeta_abi_carrier)CMETA_ABI_SCALAR);

    delta = cmeta_function_param(fn, 0u);
    check_not_null(delta);
    check_equal(delta->name, "delta");
    check_equal(delta->flags, (cmeta_param_flags)CMETA_PARAM_IN);
    check_true(cmeta_type_equal(delta->type, &cmeta_type_int));
    check_equal(cmeta_function_param_abi(abi, 0u),
                (cmeta_abi_carrier)CMETA_ABI_SCALAR);

    check_true(cmeta_interface_method_reflection_valid(&meta->methods[1]));
    check_true(cmeta_interface_method_reflection_valid(&meta->methods[2]));
}

suite("CMeta interface function reflection") {
    it("publishes canonical FunctionDesc and FunctionAbi from one method schema") {
        check_reflected_counter(cmeta_reflected_counter_interface());
    }

    it("compares TU-local interface function semantics by value, not address") {
        const cmeta_interface_desc *local = cmeta_reflected_counter_interface();
        const cmeta_interface_desc *peer_a = cmeta_interface_function_peer_a();
        const cmeta_interface_desc *peer_b = cmeta_interface_function_peer_b();
        size_t i;

        check_reflected_counter(peer_a);
        check_reflected_counter(peer_b);

        for (i = 0u; i < local->method_count; ++i) {
            check_true(cmeta_function_desc_equal(
                local->methods[i].function, peer_a->methods[i].function));
            check_true(cmeta_function_desc_equal(
                peer_a->methods[i].function, peer_b->methods[i].function));
            check_true(cmeta_function_abi_desc_equal(
                local->methods[i].abi, peer_a->methods[i].abi));
            check_true(cmeta_function_abi_desc_equal(
                peer_a->methods[i].abi, peer_b->methods[i].abi));
        }
    }
}
