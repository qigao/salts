#ifndef CNET_BENCHMARK_STATS_H
#define CNET_BENCHMARK_STATS_H

#include <stddef.h>

typedef struct cnet_benchmark_summary {
  double median;
  double mad;
} cnet_benchmark_summary;

int cnet_benchmark_summarize(const double *values, size_t count,
                             cnet_benchmark_summary *out_summary);
int cnet_benchmark_summarize_paired_delta(const double *baseline, const double *candidate,
                                          size_t count, cnet_benchmark_summary *out_summary);

#endif
