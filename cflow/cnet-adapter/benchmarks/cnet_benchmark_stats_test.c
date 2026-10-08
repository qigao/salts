#include "cnet_benchmark_stats.h"
#include <salts/error_codes.h>
#include <stdint.h>
#include <stdio.h>
int main(void) {
    const uint64_t src[] = {9,1,7,3,5,2,6,4,8,10};
    uint64_t scratch[10] = {0};
    cflow_cnet_bench_summary s = {0};
    if (cflow_cnet_bench_summarize(src, 10, scratch, 10, &s) != SALTS_OK ||
        s.samples != 10 || s.p50_ns != 5 || s.p95_ns != 10 ||
        s.p99_ns != 10 || s.max_ns != 10 || src[0] != 9) return 1;
    if (cflow_cnet_bench_summarize(src, 10, scratch, 9, &s) != SALTS_EINVAL) return 2;
    if (cflow_cnet_bench_summarize(src, 0, scratch, 10, &s) != SALTS_EINVAL) return 3;
    if (cflow_cnet_bench_summarize(src, 10, (uint64_t *)src, 10, &s) != SALTS_EINVAL) return 4;
    puts("cflow-cnet benchmark percentile contract OK");
    return 0;
}
