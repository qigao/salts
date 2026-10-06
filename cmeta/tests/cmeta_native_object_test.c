#include "cmeta_native_object_fixture.h"
#include "cmeta_native_targets.h"
#include "tinytest.h"

enum { NATIVE_OBJECT_BIAS = 7 };
suite("ObjectRef native specialization preserves provider and lifetime authority") {
    static native_object_payload payload;
    static cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    static cmeta_native_thunk thunk = CMETA_NATIVE_THUNK_INIT;
    static const cmeta_receiver_operation *operation;
    before_each() {
        const native_object_payload fresh = {NATIVE_OBJECT_BIAS, 0, 0, 0};
        const native_object_fixture_state state = {0, 0, CMETA_OK, CMETA_OK, NULL};
        payload = fresh;
        native_object_state = state;
        check_equal(cmeta_object_borrow_with_provider(&object, &payload, &native_object_data,
            native_object_provider.reference), CMETA_OK);
        operation = &object.operations->operations[0];
    }
    after_each() {
        check_equal(cmeta_native_thunk_destroy(&thunk), CMETA_OK);
        cmeta_object_release(&object);
    }
    it("calls the same receiver after one admission without retaining a shared object") {
        cmeta_native_binding binding = CMETA_NATIVE_BINDING_INIT;
        cmeta_invokable reference = CMETA_INVOKABLE_INIT;
        int input = -3, result = 0;
        const void *args[] = {&input};
        check_equal(cmeta_object_share(&object, &native_object_lifecycle), CMETA_OK);
        check_equal(cmeta_object_operation_invokable_bind(&object, operation, &reference), CMETA_OK);
        check_equal(cmeta_native_object_i32_admit(&object, operation, &native_object_provider, &binding), CMETA_OK);
        check_equal(cmeta_native_thunk_create(&binding, NATIVE_TEST_PAGE_BUDGET, &thunk), CMETA_OK);
        check_equal(cmeta_invokable_invoke_admitted(&reference, &result, args), CMETA_OK);
        check_equal(cmeta_native_thunk_entry(&thunk)(input), result);
        check_equal(result, NATIVE_OBJECT_BIAS + input);
        check_equal(payload.retains, 1u);
        check_equal(payload.releases, 0u);
        check_equal(native_object_state.reference_binds, 2u);
        check_equal(native_object_state.native_binds, 1u);
        check_equal(cmeta_native_thunk_destroy(&thunk), CMETA_OK);
        check_equal(payload.releases, 0u);
        cmeta_object_release(&object);
        check_equal(payload.releases, 1u);
        check_equal(payload.destroys, 0u);
    }
    it("leaves owned destruction with ObjectRef until all native borrows end") {
        cmeta_native_binding binding = CMETA_NATIVE_BINDING_INIT;
        check_equal(cmeta_object_take(&object, &native_object_lifecycle), CMETA_OK);
        check_equal(cmeta_native_object_i32_admit(&object, operation, &native_object_provider, &binding), CMETA_OK);
        check_equal(cmeta_native_thunk_create(&binding, NATIVE_TEST_PAGE_BUDGET, &thunk), CMETA_OK);
        check_equal(cmeta_native_thunk_entry(&thunk)(1), NATIVE_OBJECT_BIAS + 1);
        check_equal(cmeta_native_thunk_destroy(&thunk), CMETA_OK);
        check_equal(payload.destroys, 0u);
        cmeta_object_release(&object);
        check_equal(payload.destroys, 1u);
        check_equal(payload.retains, 0u);
    }
    it("rejects foreign capabilities before calling a specialization provider") {
        cmeta_native_binding binding = CMETA_NATIVE_BINDING_INIT;
        cmeta_native_object_provider foreign = native_object_provider;
        cmeta_receiver_operation copied = *operation;
        foreign.reference = NULL;
        check_equal(cmeta_native_object_i32_admit(&object, operation, &foreign, &binding), CMETA_TYPE_MISMATCH);
        check_equal(cmeta_native_object_i32_admit(&object, &copied, &native_object_provider, &binding), CMETA_INVALID_ARGUMENT);
        check_equal(native_object_state.reference_binds, 0u);
        check_equal(native_object_state.native_binds, 0u);
        check_equal(binding.kind, CMETA_NATIVE_INVALID);
    }
    it("propagates reference and native failures and rechecks a foreign projection") {
        cmeta_native_binding binding = CMETA_NATIVE_BINDING_INIT;
        native_object_state.reference_status = CMETA_CALLBACK_ERROR;
        check_equal(cmeta_native_object_i32_admit(&object, operation, &native_object_provider, &binding), CMETA_CALLBACK_ERROR);
        check_equal(native_object_state.native_binds, 0u);
        native_object_state.reference_status = CMETA_OK;
        native_object_state.native_status = CMETA_CAPACITY_EXCEEDED;
        check_equal(cmeta_native_object_i32_admit(&object, operation, &native_object_provider, &binding), CMETA_CAPACITY_EXCEEDED);
        check_equal(binding.kind, CMETA_NATIVE_INVALID);
        native_object_state.native_status = CMETA_OK;
        native_object_state.override_projection = FunctionAbi(native_test_identity);
        check_equal(cmeta_native_object_i32_admit(&object, operation, &native_object_provider, &binding), CMETA_TYPE_MISMATCH);
        check_equal(binding.kind, CMETA_NATIVE_INVALID);
        check_true(cmeta_object_ref_valid(&object));
    }
}
