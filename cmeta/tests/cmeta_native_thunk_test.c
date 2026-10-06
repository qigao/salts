#include "cmeta_native_targets.h"
#include "tinytest.h"
#include <limits.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

suite("CMeta exact native thunk ownership and ABI") {
    static cmeta_native_thunk thunk;
    before_each() { const cmeta_native_thunk zero = CMETA_NATIVE_THUNK_INIT; thunk = zero; }
    after_each() { check_equal(cmeta_native_thunk_destroy(&thunk), CMETA_OK); }
    it("publishes exact int calls with bounded RX memory and releases once") {
        cmeta_native_binding binding = CMETA_NATIVE_BINDING_INIT;
        cmeta_native_i32_fn entry;
        check_equal(cmeta_native_i32_admit(FunctionAbi(native_test_identity), native_test_identity, &binding), CMETA_OK);
        check_equal(cmeta_native_thunk_create(&binding, 1, &thunk), CMETA_CAPACITY_EXCEEDED);
        check_null(thunk.allocation);
        check_equal(cmeta_native_thunk_create(&binding, NATIVE_TEST_PAGE_BUDGET, &thunk), CMETA_OK);
        check_less_equal(thunk.allocation_size, (size_t)NATIVE_TEST_PAGE_BUDGET);
        entry = cmeta_native_thunk_entry(&thunk);
        check_true(entry != NULL);
        check_equal(entry(INT_MIN), INT_MIN);
        check_equal(entry(INT_MAX), INT_MAX);
        check_equal(entry(-7), -7);
        check_equal(cmeta_native_thunk_create(&binding, NATIVE_TEST_PAGE_BUDGET, &thunk), CMETA_BUSY);
#ifdef _WIN32
        MEMORY_BASIC_INFORMATION info;
        check_equal(VirtualQuery(thunk.allocation, &info, sizeof(info)), sizeof(info));
        check_equal(info.Protect, (DWORD)PAGE_EXECUTE_READ);
#endif
        check_equal(cmeta_native_thunk_destroy(&thunk), CMETA_OK);
        check_true(cmeta_native_thunk_entry(&thunk) == NULL);
        check_equal(cmeta_native_thunk_destroy(&thunk), CMETA_OK);
    }
    it("binds an explicit borrowed receiver and redirects the same address after quiescence") {
        cmeta_native_binding binding = CMETA_NATIVE_BINDING_INIT;
        cmeta_native_i32_fn address;
        int bias = 0;
        check_equal(cmeta_native_context_i32_admit(FunctionAbi(native_test_context),
            FunctionAbi(native_test_bound), native_test_context, NULL, &binding), CMETA_INVALID_ARGUMENT);
        check_equal(cmeta_native_context_i32_admit(FunctionAbi(native_test_context),
            FunctionAbi(native_test_bound), native_test_context, &bias, &binding), CMETA_OK);
        check_equal(cmeta_native_thunk_create(&binding, NATIVE_TEST_PAGE_BUDGET, &thunk), CMETA_OK);
        address = cmeta_native_thunk_entry(&thunk);
        check_equal(address(INT_MIN), INT_MIN);
        check_equal(address(INT_MAX), INT_MAX);
        for (int i = 0; i < NATIVE_TEST_REDIRECTS; ++i) {
            bias = i;
            check_equal(cmeta_native_thunk_rebind(&thunk, &binding), CMETA_OK);
            check_true(cmeta_native_thunk_entry(&thunk) == address);
            check_equal(address(-7), i - 7);
        }
        check_equal(cmeta_native_i32_admit(FunctionAbi(native_test_increment), native_test_increment, &binding), CMETA_OK);
        check_equal(cmeta_native_thunk_rebind(&thunk, &binding), CMETA_OK);
        check_true(cmeta_native_thunk_entry(&thunk) == address);
        check_equal(address(2), 3);
    }
    it("rejects ABI and effect disagreement before changing a published call") {
        cmeta_native_binding original = CMETA_NATIVE_BINDING_INIT, candidate = CMETA_NATIVE_BINDING_INIT;
        cmeta_function_abi_desc abi = *FunctionAbi(native_test_identity);
        cmeta_function_desc function = *abi.function;
        check_equal(cmeta_native_i32_admit(FunctionAbi(native_test_identity), native_test_identity, &original), CMETA_OK);
        check_equal(cmeta_native_thunk_create(&original, NATIVE_TEST_PAGE_BUDGET, &thunk), CMETA_OK);
        abi.return_carrier = CMETA_ABI_UNSPECIFIED;
        check_equal(cmeta_native_i32_admit(&abi, native_test_identity, &candidate), CMETA_TYPE_MISMATCH);
        check_equal(candidate.kind, CMETA_NATIVE_INVALID);
        abi = *FunctionAbi(native_test_identity);
        function.effects = CMETA_EFFECT_STATEFUL;
        function.properties = CMETA_PROP_NONE;
        abi.function = &function;
        check_equal(cmeta_native_i32_admit(&abi, native_test_identity, &candidate), CMETA_OK);
        check_equal(cmeta_native_thunk_rebind(&thunk, &candidate), CMETA_TYPE_MISMATCH);
        check_equal(cmeta_native_thunk_entry(&thunk)(9), 9);
    }
}
