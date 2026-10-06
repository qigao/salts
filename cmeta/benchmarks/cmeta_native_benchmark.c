#include "../tests/cmeta_native_targets.h"
#include "../tests/cmeta_native_object_fixture.h"
#include <cmeta/native/static_call.h>
#include "tinytest.h"
#include <stdlib.h>

enum { NATIVE_BENCH_SAMPLES = 25, NATIVE_BENCH_OPERATIONS = 1000000,
    NATIVE_BENCH_CONTROL_OPERATIONS = 1000, NATIVE_BENCH_BIAS = 7 };
static volatile int native_sink;
cmeta_static_thunk_call(native_bench_slot, native_test_identity);
suite("CMeta exact native specialization cost") {
    static cmeta_native_thunk thunk;
    static cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    static native_object_payload payload;
    before_each() { const cmeta_native_thunk zero = CMETA_NATIVE_THUNK_INIT; thunk = zero; }
    after_each() {
        check_equal(cmeta_static_update(native_bench_slot, native_test_identity), CMETA_OK);
        check_equal(cmeta_native_thunk_destroy(&thunk), CMETA_OK);
        cmeta_object_release(&object);
    }
    bench("compares the same borrowed target and measures control-plane cost separately") {
        int bias = NATIVE_BENCH_BIAS;
        cmeta_native_binding binding = CMETA_NATIVE_BINDING_INIT;
        cmeta_invokable generated = CMETA_INVOKABLE_INIT;
        native_test_bound_capture capture = {&bias};
        cmeta_native_i32_fn entry;
        check_equal(native_test_bound_bind(&capture, &generated), CMETA_OK);
        check_equal(cmeta_native_context_i32_admit(FunctionAbi(native_test_context),
            FunctionAbi(native_test_bound), native_test_context, &bias, &binding), CMETA_OK);
        check_equal(cmeta_native_thunk_create(&binding, NATIVE_TEST_PAGE_BUDGET, &thunk), CMETA_OK);
        entry = cmeta_native_thunk_entry(&thunk);
        benchmark_ops("admitted generated receiver", NATIVE_BENCH_SAMPLES, NATIVE_BENCH_OPERATIONS) {
            int output = 0;
            for (int i = 0; i < NATIVE_BENCH_OPERATIONS; ++i) {
                const void *args[] = {&i};
                if (cmeta_invokable_invoke_admitted(&generated, &output, args) != CMETA_OK) abort();
            }
            native_sink = output;
        }
        check_equal(native_sink, NATIVE_BENCH_OPERATIONS - 1 + bias);
        benchmark_ops("native bound receiver", NATIVE_BENCH_SAMPLES, NATIVE_BENCH_OPERATIONS) {
            int output = 0;
            for (int i = 0; i < NATIVE_BENCH_OPERATIONS; ++i) output = entry(i);
            native_sink = output;
        }
        check_equal(native_sink, NATIVE_BENCH_OPERATIONS - 1 + bias);
        check_equal(cmeta_static_thunk_update(native_bench_slot, &thunk), CMETA_OK);
        benchmark_ops("native receiver through static-call slot", NATIVE_BENCH_SAMPLES, NATIVE_BENCH_OPERATIONS) {
            int output = 0;
            for (int i = 0; i < NATIVE_BENCH_OPERATIONS; ++i)
                output = cmeta_static_invoke(native_bench_slot, i);
            native_sink = output;
        }
        check_equal(native_sink, NATIVE_BENCH_OPERATIONS - 1 + bias);
        check_equal(cmeta_static_update(native_bench_slot, native_test_identity), CMETA_OK);
        benchmark_ops("quiescent native rebind", NATIVE_BENCH_SAMPLES, NATIVE_BENCH_CONTROL_OPERATIONS) {
            for (int i = 0; i < NATIVE_BENCH_CONTROL_OPERATIONS; ++i)
                if (cmeta_native_thunk_rebind(&thunk, &binding) != CMETA_OK) abort();
        }
        check_equal(cmeta_native_thunk_entry(&thunk)(1), bias + 1);
        check_equal(cmeta_native_thunk_destroy(&thunk), CMETA_OK);
        benchmark_ops("native create and destroy", NATIVE_BENCH_SAMPLES, NATIVE_BENCH_CONTROL_OPERATIONS) {
            for (int i = 0; i < NATIVE_BENCH_CONTROL_OPERATIONS; ++i) {
                if (cmeta_native_thunk_create(&binding, NATIVE_TEST_PAGE_BUDGET, &thunk) != CMETA_OK) abort();
                if (cmeta_native_thunk_destroy(&thunk) != CMETA_OK) abort();
            }
        }
    }
    bench("compares ObjectRef reference and native execution with the same provider") {
        const native_object_payload fresh = {NATIVE_BENCH_BIAS, 0, 0, 0};
        const native_object_fixture_state state = {0, 0, CMETA_OK, CMETA_OK, NULL};
        cmeta_native_binding binding = CMETA_NATIVE_BINDING_INIT;
        cmeta_invokable reference = CMETA_INVOKABLE_INIT;
        const cmeta_receiver_operation *operation;
        cmeta_native_i32_fn entry;
        payload = fresh;
        native_object_state = state;
        check_equal(cmeta_object_borrow_with_provider(&object, &payload, &native_object_data,
            native_object_provider.reference), CMETA_OK);
        check_equal(cmeta_object_share(&object, &native_object_lifecycle), CMETA_OK);
        operation = &object.operations->operations[0];
        check_equal(cmeta_object_operation_invokable_bind(&object, operation, &reference), CMETA_OK);
        check_equal(cmeta_native_object_i32_admit(&object, operation, &native_object_provider, &binding), CMETA_OK);
        check_equal(cmeta_native_thunk_create(&binding, NATIVE_TEST_PAGE_BUDGET, &thunk), CMETA_OK);
        entry = cmeta_native_thunk_entry(&thunk);
        benchmark_ops("admitted ObjectRef receiver", NATIVE_BENCH_SAMPLES, NATIVE_BENCH_OPERATIONS) {
            int output = 0;
            for (int i = 0; i < NATIVE_BENCH_OPERATIONS; ++i) {
                const void *args[] = {&i};
                if (cmeta_invokable_invoke_admitted(&reference, &output, args) != CMETA_OK) abort();
            }
            native_sink = output;
        }
        check_equal(native_sink, NATIVE_BENCH_OPERATIONS - 1 + payload.bias);
        benchmark_ops("native ObjectRef receiver", NATIVE_BENCH_SAMPLES, NATIVE_BENCH_OPERATIONS) {
            int output = 0;
            for (int i = 0; i < NATIVE_BENCH_OPERATIONS; ++i) output = entry(i);
            native_sink = output;
        }
        check_equal(native_sink, NATIVE_BENCH_OPERATIONS - 1 + payload.bias);
        check_equal(payload.retains, 1u);
        check_equal(payload.releases, 0u);
        check_equal(native_object_state.reference_binds, 2u);
        check_equal(native_object_state.native_binds, 1u);
        check_equal(cmeta_native_thunk_destroy(&thunk), CMETA_OK);
        cmeta_object_release(&object);
        check_equal(payload.releases, 1u);
    }
}
