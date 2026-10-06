#include "../tests/cmeta_native_targets.h"
#include "tinytest.h"
#include <stdlib.h>

enum { NATIVE_BENCH_SAMPLES = 25, NATIVE_BENCH_OPERATIONS = 1000000,
    NATIVE_BENCH_CONTROL_OPERATIONS = 1000, NATIVE_BENCH_BIAS = 7 };
static volatile int native_sink;
suite("CMeta exact native specialization cost") {
    static cmeta_native_thunk thunk;
    before_each() { const cmeta_native_thunk zero = CMETA_NATIVE_THUNK_INIT; thunk = zero; }
    after_each() { check_equal(cmeta_native_thunk_destroy(&thunk), CMETA_OK); }
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
}
