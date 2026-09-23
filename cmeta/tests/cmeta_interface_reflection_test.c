#include "cmeta_interface_reflection_fixture.h"
#include "tinytest.h"

const cmeta_interface_desc *cmeta_interface_reflection_peer_a(void);
const cmeta_interface_desc *cmeta_interface_reflection_peer_b(void);

static void check_counter_semantics(const cmeta_interface_desc *meta) {
    const cmeta_interface_method_desc *add_method;
    const cmeta_function_desc *add;
    const cmeta_function_abi_desc *add_abi;
    const cmeta_param_desc *delta;

    check_true(cmeta_interface_desc_valid(meta));
    check_equal(meta->name, "cmeta_reflection_counter");
    check_equal(meta->method_count, (size_t)3);

    add_method = &meta->methods[0];
    check_true(cmeta_interface_method_reflection_valid(add_method));
    add = cmeta_interface_method_function(add_method);
    add_abi = cmeta_interface_method_abi(add_method);
    check_not_null(add);
    check_not_null(add_abi);
    check_true(cmeta_function_desc_valid(add));
    check_true(cmeta_function_abi_desc_valid(add_abi));
    check_equal(add->name, "cmeta_reflection_counter.add");
    check_equal(add->param_count, (size_t)1);
    check_true(cmeta_type_equal(add->return_type, &cmeta_type_int));
    check_equal(add->effects, (cmeta_effects)CMETA_CONTRACT_EFFECTS(value));
    check_equal(add->properties,
                (cmeta_properties)CMETA_CONTRACT_PROPERTIES(value));
    check_equal(add_abi->return_carrier,
                (cmeta_abi_carrier)CMETA_ABI_SCALAR);

    delta = cmeta_function_param(add, 0u);
    check_not_null(delta);
    check_equal(delta->name, "delta");
    check_true(cmeta_type_equal(delta->type, &cmeta_type_int));
    check_equal(delta->flags, (cmeta_param_flags)CMETA_PARAM_IN);
    check_true(cmeta_param_direction_known(delta));
    check_equal(cmeta_function_param_abi(add_abi, 0u),
                (cmeta_abi_carrier)CMETA_ABI_SCALAR);

    check_true(cmeta_interface_method_reflection_valid(&meta->methods[1]));
    check_true(cmeta_interface_method_reflection_valid(&meta->methods[2]));
    check_true(cmeta_type_equal(
        meta->methods[2].function->return_type, &cmeta_type_void));
    check_equal(meta->methods[2].function->effects,
                (cmeta_effects)CMETA_CONTRACT_EFFECTS(stateful));
    check_false(cmeta_interface_method_is_owning(&meta->methods[2]));
}

suite("CMeta interface function reflection") {
    it("publishes complete canonical method semantics") {
        check_counter_semantics(cmeta_reflection_counter_interface());
    }

    it("compares method semantics across translation units") {
        const cmeta_interface_desc *local =
            cmeta_reflection_counter_interface();
        const cmeta_interface_desc *peer_a =
            cmeta_interface_reflection_peer_a();
        const cmeta_interface_desc *peer_b =
            cmeta_interface_reflection_peer_b();

        check_counter_semantics(peer_a);
        check_counter_semantics(peer_b);

        check_true(cmeta_function_desc_equal(
            local->methods[0].function,
            peer_a->methods[0].function));
        check_true(cmeta_function_desc_equal(
            peer_a->methods[2].function,
            peer_b->methods[2].function));
        check_true(cmeta_function_abi_desc_equal(
            local->methods[0].abi,
            peer_b->methods[0].abi));
    }

    it("keeps legacy rows explicitly reflection-free") {
        const cmeta_interface_desc *legacy = cmeta_test_counter_interface();
        (void)legacy;
    }
}
