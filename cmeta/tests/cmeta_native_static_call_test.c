#include <cmeta/native/static_call.h>
#include "cmeta_native_targets.h"
#include "tinytest.h"

cmeta_static_thunk_call(native_static_slot, native_test_identity);
enum { NATIVE_STATIC_BIAS = 7 };
suite("Native opt-in over the canonical static-call slot") {
    static cmeta_native_thunk thunk = CMETA_NATIVE_THUNK_INIT;
    after_each() {
        /* The suite is single-threaded: publication ends all old calls here. */
        check_equal(cmeta_static_update(native_static_slot, native_test_identity), CMETA_OK);
        check_equal(cmeta_native_thunk_destroy(&thunk), CMETA_OK);
    }
    it("publishes an exact receiver and restores the ordinary target before release") {
        int bias = NATIVE_STATIC_BIAS;
        cmeta_native_binding binding = CMETA_NATIVE_BINDING_INIT;
        const cmeta_native_thunk *witness = &thunk;
        check_equal(cmeta_native_context_i32_admit(FunctionAbi(native_test_context),
            FunctionAbi(native_test_bound), native_test_context, &bias, &binding), CMETA_OK);
        check_equal(cmeta_native_thunk_create(&binding, NATIVE_TEST_PAGE_BUDGET, &thunk), CMETA_OK);
        check_equal(cmeta_static_thunk_update(native_static_slot, witness++), CMETA_OK);
        check_true(witness == &thunk + 1);
        check_equal(cmeta_static_invoke(native_static_slot, -3), bias - 3);
        check_equal(cmeta_static_update(native_static_slot, native_test_increment), CMETA_OK);
        check_equal(cmeta_native_thunk_destroy(&thunk), CMETA_OK);
        check_equal(cmeta_static_invoke(native_static_slot, -3), -2);
    }
    it("keeps the slot unchanged when code is unpublished or its effects differ") {
        cmeta_native_binding binding = CMETA_NATIVE_BINDING_INIT;
        cmeta_function_desc function = *FunctionMeta(native_test_identity);
        cmeta_function_abi_desc abi = *FunctionAbi(native_test_identity);
        check_equal(cmeta_static_thunk_update(native_static_slot, &thunk), CMETA_INVALID_ARGUMENT);
        function.effects = CMETA_EFFECT_STATEFUL;
        function.properties = CMETA_PROP_NONE;
        abi.function = &function;
        check_equal(cmeta_native_i32_admit(&abi, native_test_increment, &binding), CMETA_OK);
        check_equal(cmeta_native_thunk_create(&binding, NATIVE_TEST_PAGE_BUDGET, &thunk), CMETA_OK);
        check_equal(cmeta_static_thunk_update(native_static_slot, &thunk), CMETA_TYPE_MISMATCH);
        check_equal(cmeta_static_invoke(native_static_slot, 3), 3);
        /* ABI metadata is stack-owned and must survive destruction too. */
        check_equal(cmeta_native_thunk_destroy(&thunk), CMETA_OK);
    }
}
