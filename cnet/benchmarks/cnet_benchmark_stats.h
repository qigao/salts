#ifndef CNET_BENCHMARK_STATS_H
#define CNET_BENCHMARK_STATS_H

#include <stddef.h>
#include <stdint.h>

typedef struct cnet_benchmark_summary {
  double median;
  double mad;
} cnet_benchmark_summary;

/* Non-overlapping outer benchmark spans, not CPU-only or causal attribution. */
typedef struct cnet_benchmark_phase_sample {
  uint64_t wall_ns;
  uint64_t start_ns;
  uint64_t drive_ns;
  uint64_t check_ns;
} cnet_benchmark_phase_sample;

typedef struct cnet_benchmark_phase_budget {
  double wall_ns;
  double start_ns;
  double drive_ns;
  double check_ns;
  double remainder_ns;
} cnet_benchmark_phase_budget;

int cnet_benchmark_phase_decompose(const cnet_benchmark_phase_sample *sample, size_t round_trips,
                                  cnet_benchmark_phase_budget *out_budget);

/** Totals collected during one diagnostic repeat. */
typedef struct cnet_benchmark_fixed_control_sample {
  size_t round_trips;
  uint64_t send_admit_ns;
  uint64_t queue_publish_ns;
  uint64_t payload_copy_ns;
  uint64_t client_poll_ns;
  uint64_t owner_drive_ns;
  uint64_t request_lifecycle_ns;
  uint64_t request_start_ns;
  uint64_t request_resubmit_ns;
  uint64_t observe_ns;
  uint64_t request_completion_ns;
  uint64_t event_publish_ns;
  uint64_t dispatcher_prepare_ns;
  uint64_t dispatcher_invoke_ns;
  uint64_t dispatcher_observer_ns;
  uint64_t dispatcher_release_ns;
  uint64_t benchmark_callback_ns;
  uint64_t benchmark_payload_check_ns;
} cnet_benchmark_fixed_control_sample;

/**
 * Per-round-trip exclusive decomposition of one diagnostic CNet sample.
 * Nested inclusive clocks are reduced to exclusive leaves before totals are
 * formed. `closure_residual_ns` must stay near zero; it is not an optimization
 * target.
 */
typedef struct cnet_benchmark_fixed_control_attribution {
  double total_budget_ns;
  double send_public_control_ns;
  double queue_control_ns;
  double payload_copy_ns;
  double client_poll_wrapper_ns;
  double owner_control_ns;
  double request_control_ns;
  double native_request_start_ns;
  double native_request_resubmit_ns;
  double native_observe_ns;
  double completion_control_ns;
  double event_publish_residual_ns;
  double dispatcher_prepare_ns;
  double dispatcher_invoke_framework_ns;
  double client_observer_control_ns;
  double dispatcher_release_ns;
  double benchmark_payload_check_ns;
  double benchmark_callback_residual_ns;
  double fixed_control_total_ns;
  double shared_native_total_ns;
  double benchmark_work_total_ns;
  double closure_residual_ns;
} cnet_benchmark_fixed_control_attribution;

int cnet_benchmark_summarize(const double *values, size_t count,
                             cnet_benchmark_summary *out_summary);
int cnet_benchmark_summarize_paired_delta(const double *baseline, const double *candidate,
                                          size_t count, cnet_benchmark_summary *out_summary);

int cnet_benchmark_attribute_fixed_control(
    const cnet_benchmark_fixed_control_sample *sample,
    cnet_benchmark_fixed_control_attribution *out_attribution);

#endif
