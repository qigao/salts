#include "cnet_benchmark_stats.h"
#include <salts/error_codes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static int compare_u64(const void *a, const void *b) {
    const uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}
static size_t rank_index(size_t count, size_t percent) {
    /* ceil(count * percent / 100) - 1 without overflow. */
    const size_t q = count / 100u, r = count % 100u;
    size_t k = q * percent + (r * percent + 99u) / 100u;
    return k ? k - 1u : 0u;
}
int cflow_cnet_bench_summarize(const uint64_t *samples, size_t count,
                              uint64_t *scratch, size_t scratch_capacity,
                              cflow_cnet_bench_summary *out) {
    if (!samples || !scratch || !out || !count ||
        scratch_capacity < count || count > SIZE_MAX / sizeof(uint64_t) ||
        samples == scratch) return SALTS_EINVAL;
    for (size_t i = 0; i < count; ++i)
        if (!samples[i]) return SALTS_ERANGE;
    memcpy(scratch, samples, count * sizeof(uint64_t));
    qsort(scratch, count, sizeof(uint64_t), compare_u64);
    *out = (cflow_cnet_bench_summary) {
        scratch[rank_index(count, 50u)],
        scratch[rank_index(count, 95u)],
        scratch[rank_index(count, 99u)],
        scratch[count - 1u], (uint64_t)count
    };
    return SALTS_OK;
}
