#include <cmeta/trace.h>
#include "tinytest.h"

enum { TRACE_BENCH_SAMPLES = 25, TRACE_BENCH_OPERATIONS = 1000000 };
static volatile bool trace_plain_enabled;
static volatile uint64_t trace_benchmark_sink;
SALTS_FAST_KEY(trace_benchmark_key, false);
cmeta_fault_point(trace_benchmark_fault);
cmeta_tracepoint(trace_benchmark_event, cmeta_field(uint64_t, sequence));
static void trace_benchmark_backend(const trace_benchmark_event_payload *event) {
    trace_benchmark_sink += event->sequence;
}

suite("CMeta trace and fault Release benchmarks") {
    bench("compares disabled trace and fault gates with plain branch and static key") {
        check_equal(cmeta_trace_bind(trace_benchmark_event, trace_benchmark_backend), CMETA_OK);
        check_equal(cmeta_trace_disable(trace_benchmark_event), CMETA_OK);
        check_equal(cmeta_fault_disarm(trace_benchmark_fault), CMETA_OK);
        benchmark_ops("plain disabled branch", TRACE_BENCH_SAMPLES, TRACE_BENCH_OPERATIONS) {
            uint64_t sum = 0u;
            for (uint64_t i = 0u; i < TRACE_BENCH_OPERATIONS; ++i)
                if (trace_plain_enabled) sum += i;
            trace_benchmark_sink = sum;
        }
        check_equal(trace_benchmark_sink, UINT64_C(0));
        benchmark_ops("disabled static key", TRACE_BENCH_SAMPLES, TRACE_BENCH_OPERATIONS) {
            uint64_t sum = 0u;
            for (uint64_t i = 0u; i < TRACE_BENCH_OPERATIONS; ++i)
                if (salts_fast_branch(&trace_benchmark_key)) sum += i;
            trace_benchmark_sink = sum;
        }
        check_equal(trace_benchmark_sink, UINT64_C(0));
        benchmark_ops("disabled typed trace", TRACE_BENCH_SAMPLES, TRACE_BENCH_OPERATIONS) {
            trace_benchmark_sink = 0u;
            for (uint64_t i = 0u; i < TRACE_BENCH_OPERATIONS; ++i)
                cmeta_trace_emit(trace_benchmark_event, i);
        }
        check_equal(trace_benchmark_sink, UINT64_C(0));
        benchmark_ops("disabled fault point", TRACE_BENCH_SAMPLES, TRACE_BENCH_OPERATIONS) {
            uint64_t sum = 0u;
            for (uint64_t i = 0u; i < TRACE_BENCH_OPERATIONS; ++i)
                if (cmeta_fault_hit(&trace_benchmark_fault)) sum += i;
            trace_benchmark_sink = sum;
        }
        check_equal(trace_benchmark_sink, UINT64_C(0));
    }
    bench("emits typed payloads with a synchronous backend") {
        const uint64_t expected = (uint64_t)TRACE_BENCH_OPERATIONS *
            ((uint64_t)TRACE_BENCH_OPERATIONS - 1u) / 2u;
        check_equal(cmeta_trace_enable(trace_benchmark_event), CMETA_OK);
        benchmark_ops("enabled typed trace", TRACE_BENCH_SAMPLES, TRACE_BENCH_OPERATIONS) {
            trace_benchmark_sink = 0u;
            for (uint64_t i = 0u; i < TRACE_BENCH_OPERATIONS; ++i)
                cmeta_trace_emit(trace_benchmark_event, i);
        }
        check_equal(trace_benchmark_sink, expected);
        check_equal(cmeta_trace_disable(trace_benchmark_event), CMETA_OK);
    }
}
