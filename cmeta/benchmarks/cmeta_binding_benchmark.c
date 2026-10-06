#include <cmeta/bind.h>
#include "tinytest.h"
#include <stdlib.h>

enum { BIND_SAMPLES = 25, BIND_OPERATIONS = 1000000, BIND_BIAS = 7 };
static volatile int bind_sink;
FunctionDeclAsAbiResult(value, int, &cmeta_type_int, CMETA_ABI_SCALAR, CMETA_RESULT_VALUE,
    cmeta_bench_add, (int, left, CMETA_PARAM_IN, &cmeta_type_int, CMETA_ABI_SCALAR),
    (int, right, CMETA_PARAM_IN, &cmeta_type_int, CMETA_ABI_SCALAR));
FunctionBindDeclAsAbiResult(value, int, &cmeta_type_int, CMETA_ABI_SCALAR, CMETA_RESULT_VALUE,
    cmeta_bench_bound, cmeta_bench_add, CMETA_SIG_U_I_I,
    (value, (int, left, CMETA_PARAM_IN, &cmeta_type_int, CMETA_ABI_SCALAR)),
    (arg, (int, right, CMETA_PARAM_IN, &cmeta_type_int, CMETA_ABI_SCALAR)));

suite("CMeta binding baseline") {
    bench("measures exact native call and admitted generated binding") {
        cmeta_bench_bound_capture capture = {BIND_BIAS};
        cmeta_invokable bound = CMETA_INVOKABLE_INIT;
        check_equal(cmeta_bench_bound_bind(&capture, &bound), CMETA_OK);
        benchmark_ops("direct exact C", BIND_SAMPLES, BIND_OPERATIONS) {
            int output = 0;
            for (int i = 0; i < BIND_OPERATIONS; ++i) output = cmeta_bench_add(BIND_BIAS, i);
            bind_sink = output;
        }
        check_equal(bind_sink, BIND_OPERATIONS - 1 + BIND_BIAS);
        benchmark_ops("admitted generated bind", BIND_SAMPLES, BIND_OPERATIONS) {
            int output = 0;
            for (int i = 0; i < BIND_OPERATIONS; ++i) {
                const void *args[] = {&i};
                if (cmeta_invokable_invoke_admitted(&bound, &output, args) != CMETA_OK) abort();
            }
            bind_sink = output;
        }
        check_equal(bind_sink, BIND_OPERATIONS - 1 + BIND_BIAS);
    }
}
