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

/**
 * Additive attribution of exclusive fixed-control leaves inside one separately
 * measured control envelope. `residual_ns` is whatever part of the envelope is
 * not explained by the supplied non-overlapping leaves.
 */
typedef struct cnet_benchmark_fixed_control_attribution {
  double control_envelope_ns;
  double accounted_control_ns;
  double residual_ns;
} cnet_benchmark_fixed_control_attribution;

int cnet_benchmark_summarize(const double *values, size_t count,
                             cnet_benchmark_summary *out_summary);
int cnet_benchmark_summarize_paired_delta(const double *baseline, const double *candidate,
                                          size_t count, cnet_benchmark_summary *out_summary);

int cnet_benchmark_attribute_send(uint64_t send_admit_ns, uint64_t send_admit_calls,
                                  uint64_t queue_publish_ns, uint64_t queue_publish_calls,
                                  uint64_t payload_copy_ns, uint64_t payload_copy_calls,
                                  cnet_benchmark_send_attribution *out_attribution);

/**
 * Derives the non-overlapping fixed-control envelope for one diagnostic sample.
 * Request completion executes after coroutine await resumes and is therefore
 * nested inside observe; subtract only observe-exclusive native execution.
 */
int cnet_benchmark_fixed_control_envelope(
    double send_admit_ns, double payload_copy_ns, double poll_ns, double request_start_ns,
    double observe_ns, double request_completion_ns, double payload_check_ns,
    double *out_envelope_ns);

int cnet_benchmark_attribute_fixed_control(
    double control_envelope_ns, double public_admission_control_ns,
    double queue_staging_control_ns, double poll_control_ns, double receive_rearm_control_ns,
    double command_control_ns, double owner_residual_ns, double request_control_ns,
    double completion_control_ns, double event_and_callback_control_ns,
    cnet_benchmark_fixed_control_attribution *out_attribution);

#endif
