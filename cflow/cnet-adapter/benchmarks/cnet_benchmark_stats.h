#ifndef CFLOW_CNET_BENCHMARK_STATS_H
#define CFLOW_CNET_BENCHMARK_STATS_H
#include <stddef.h>
#include <stdint.h>
typedef struct cflow_cnet_bench_summary {
    uint64_t p50_ns, p95_ns, p99_ns, max_ns;
    uint64_t samples;
} cflow_cnet_bench_summary;
/* Input is one bounded run's callback-to-business-settlement latency.
 * Nearest-rank percentiles; does not mutate input. All samples must be >0.
 * Scratch storage is provided by caller: no hidden per-item allocations. */
int cflow_cnet_bench_summarize(const uint64_t *samples, size_t count,
                              uint64_t *scratch, size_t scratch_capacity,
                              cflow_cnet_bench_summary *out);
#endif
