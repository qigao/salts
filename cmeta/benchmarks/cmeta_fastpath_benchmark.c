#include "cmeta_fastpath_benchmark_target.h"
#include "tinytest.h"

enum { FASTPATH_BENCH_SAMPLES = 25, FASTPATH_BENCH_OPERATIONS = 1000000 };
static volatile bool fastpath_plain_enabled;
static volatile uint64_t fastpath_benchmark_sink;
SALTS_FAST_KEY(fastpath_benchmark_key, false);
cmeta_static_call(fastpath_benchmark_slot, fastpath_benchmark_target);

suite("CMeta fastpath Release benchmarks") {
    bench("compares disabled and enabled steady-state gates") {
        const uint64_t enabled_sum = (uint64_t)FASTPATH_BENCH_OPERATIONS *
            ((uint64_t)FASTPATH_BENCH_OPERATIONS + 1u) / 2u;
        for (int enabled = 0; enabled <= 1; ++enabled) {
            fastpath_plain_enabled = enabled != 0;
            check_equal(salts_fast_key_set(&fastpath_benchmark_key, enabled != 0), CMETA_OK);
            benchmark_ops(enabled ? "plain branch / enabled" : "plain branch / disabled",
                          FASTPATH_BENCH_SAMPLES, FASTPATH_BENCH_OPERATIONS) {
                uint64_t sum = 0u;
                for (uint64_t index = 0u; index < FASTPATH_BENCH_OPERATIONS; ++index)
                    if (fastpath_plain_enabled) sum += fastpath_benchmark_target(index);
                fastpath_benchmark_sink = sum;
            }
            check_equal(fastpath_benchmark_sink, enabled ? enabled_sum : UINT64_C(0));
            benchmark_ops(enabled ? "reference key / enabled" : "reference key / disabled",
                          FASTPATH_BENCH_SAMPLES, FASTPATH_BENCH_OPERATIONS) {
                uint64_t sum = 0u;
                for (uint64_t index = 0u; index < FASTPATH_BENCH_OPERATIONS; ++index)
                    if (salts_fast_branch(&fastpath_benchmark_key))
                        sum += fastpath_benchmark_target(index);
                fastpath_benchmark_sink = sum;
            }
            check_equal(fastpath_benchmark_sink, enabled ? enabled_sum : UINT64_C(0));
#if SALTS_PLATFORM_NATIVE_FASTPATH
            benchmark_ops(enabled ? "native key / enabled" : "native key / disabled",
                          FASTPATH_BENCH_SAMPLES, FASTPATH_BENCH_OPERATIONS) {
                uint64_t sum = 0u;
                for (uint64_t index = 0u; index < FASTPATH_BENCH_OPERATIONS; ++index)
                    if (salts_fast_key_read_native(&fastpath_benchmark_key))
                        sum += fastpath_benchmark_target(index);
                fastpath_benchmark_sink = sum;
            }
            check_equal(fastpath_benchmark_sink, enabled ? enabled_sum : UINT64_C(0));
#endif
        }
    }
    bench("compares direct and atomic target calls") {
        const uint64_t expected = (uint64_t)FASTPATH_BENCH_OPERATIONS *
            ((uint64_t)FASTPATH_BENCH_OPERATIONS + 1u) / 2u;
        benchmark_ops("direct C call", FASTPATH_BENCH_SAMPLES, FASTPATH_BENCH_OPERATIONS) {
            uint64_t sum = 0u;
            for (uint64_t index = 0u; index < FASTPATH_BENCH_OPERATIONS; ++index)
                sum += fastpath_benchmark_target(index);
            fastpath_benchmark_sink = sum;
        }
        check_equal(fastpath_benchmark_sink, expected);
        benchmark_ops("reference static call", FASTPATH_BENCH_SAMPLES, FASTPATH_BENCH_OPERATIONS) {
            uint64_t sum = 0u;
            for (uint64_t index = 0u; index < FASTPATH_BENCH_OPERATIONS; ++index)
                sum += cmeta_static_invoke(fastpath_benchmark_slot, index);
            fastpath_benchmark_sink = sum;
        }
        check_equal(fastpath_benchmark_sink, expected);
#if SALTS_PLATFORM_NATIVE_FASTPATH
        benchmark_ops("native static call", FASTPATH_BENCH_SAMPLES, FASTPATH_BENCH_OPERATIONS) {
            uint64_t sum = 0u;
            for (uint64_t index = 0u; index < FASTPATH_BENCH_OPERATIONS; ++index)
                sum += cmeta_static_native_invoke(fastpath_benchmark_slot, index);
            fastpath_benchmark_sink = sum;
        }
        check_equal(fastpath_benchmark_sink, expected);
#endif
    }
}
