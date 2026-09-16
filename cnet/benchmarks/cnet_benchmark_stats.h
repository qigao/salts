#ifndef CNET_BENCHMARK_STATS_H
#define CNET_BENCHMARK_STATS_H

#include <stddef.h>
#include <stdint.h>

typedef struct cnet_benchmark_summary {
  double median;
  double mad;
} cnet_benchmark_summary;

/**
 * Per-call producer-side send admission attribution. `queue_publish_ns` is
 * inclusive and equals queue staging/control plus payload copy. The exclusive
 * decomposition is:
 *
 *   send_admit = public_control + queue_publish
 *   queue_publish = queue_staging_control + payload_copy
 */
typedef struct cnet_benchmark_send_attribution {
  double send_admit_ns;
  double public_control_ns;
  double queue_publish_ns;
  double queue_staging_control_ns;
  double payload_copy_ns;
} cnet_benchmark_send_attribution;

int cnet_benchmark_summarize(const double *values, size_t count,
                             cnet_benchmark_summary *out_summary);
int cnet_benchmark_summarize_paired_delta(const double *baseline, const double *candidate,
                                          size_t count, cnet_benchmark_summary *out_summary);

int cnet_benchmark_attribute_send(uint64_t send_admit_ns, uint64_t send_admit_calls,
                                  uint64_t queue_publish_ns, uint64_t queue_publish_calls,
                                  uint64_t payload_copy_ns, uint64_t payload_copy_calls,
                                  cnet_benchmark_send_attribution *out_attribution);

#endif
