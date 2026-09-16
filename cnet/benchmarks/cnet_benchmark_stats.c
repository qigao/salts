#include "cnet_benchmark_stats.h"

#include <salts/error_codes.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static int cnet_benchmark_double_compare(const void *left, const void *right) {
  const double lhs = *(const double *)left;
  const double rhs = *(const double *)right;
  return lhs < rhs ? -1 : lhs > rhs;
}

static double cnet_benchmark_median(const double *ordered, size_t count) {
  const size_t middle = count / 2u;
  return count % 2u != 0u ? ordered[middle]
                          : ordered[middle - 1u] + (ordered[middle] - ordered[middle - 1u]) / 2.0;
}

static int cnet_benchmark_summarize_impl(const double *values, size_t count, bool require_positive,
                                         cnet_benchmark_summary *out_summary) {
  double *scratch;
  double median;

  if (values == NULL || out_summary == NULL || count == 0u) return SALTS_EINVAL;
  if (count > SIZE_MAX / sizeof(*scratch)) return SALTS_ERANGE;
  for (size_t index = 0u; index < count; ++index) {
    if (!isfinite(values[index]) || (require_positive && values[index] <= 0.0)) return SALTS_ERANGE;
  }
  scratch = (double *)malloc(count * sizeof(*scratch));
  if (scratch == NULL) return SALTS_ENOMEM;
  for (size_t index = 0u; index < count; ++index)
    scratch[index] = values[index];
  qsort(scratch, count, sizeof(*scratch), cnet_benchmark_double_compare);
  median = cnet_benchmark_median(scratch, count);
  for (size_t index = 0u; index < count; ++index)
    scratch[index] = fabs(values[index] - median);
  qsort(scratch, count, sizeof(*scratch), cnet_benchmark_double_compare);
  *out_summary = (cnet_benchmark_summary){median, cnet_benchmark_median(scratch, count)};
  free(scratch);
  return SALTS_OK;
}

int cnet_benchmark_summarize(const double *values, size_t count,
                             cnet_benchmark_summary *out_summary) {
  return cnet_benchmark_summarize_impl(values, count, true, out_summary);
}

int cnet_benchmark_summarize_paired_delta(const double *baseline, const double *candidate,
                                          size_t count, cnet_benchmark_summary *out_summary) {
  double *deltas;
  int status;

  if (baseline == NULL || candidate == NULL || out_summary == NULL || count == 0u)
    return SALTS_EINVAL;
  if (count > SIZE_MAX / sizeof(*deltas)) return SALTS_ERANGE;
  deltas = (double *)malloc(count * sizeof(*deltas));
  if (deltas == NULL) return SALTS_ENOMEM;
  for (size_t index = 0u; index < count; ++index) {
    if (!isfinite(baseline[index]) || baseline[index] <= 0.0 || !isfinite(candidate[index]) ||
        candidate[index] <= 0.0) {
      free(deltas);
      return SALTS_ERANGE;
    }
    deltas[index] = (candidate[index] / baseline[index] - 1.0) * 100.0;
  }
  status = cnet_benchmark_summarize_impl(deltas, count, false, out_summary);
  free(deltas);
  return status;
}

int cnet_benchmark_attribute_send(uint64_t send_admit_ns, uint64_t send_admit_calls,
                                  uint64_t queue_publish_ns, uint64_t queue_publish_calls,
                                  uint64_t payload_copy_ns, uint64_t payload_copy_calls,
                                  cnet_benchmark_send_attribution *out_attribution) {
  double calls;

  if (out_attribution == NULL || send_admit_calls == 0u || queue_publish_calls == 0u ||
      payload_copy_calls == 0u)
    return SALTS_EINVAL;
  if (send_admit_calls != queue_publish_calls || send_admit_calls != payload_copy_calls ||
      send_admit_ns < queue_publish_ns || queue_publish_ns < payload_copy_ns)
    return SALTS_ERANGE;

  calls = (double)send_admit_calls;
  *out_attribution = (cnet_benchmark_send_attribution){
      (double)send_admit_ns / calls,
      (double)(send_admit_ns - queue_publish_ns) / calls,
      (double)queue_publish_ns / calls,
      (double)(queue_publish_ns - payload_copy_ns) / calls,
      (double)payload_copy_ns / calls,
  };
  return SALTS_OK;
}

int cnet_benchmark_attribute_fixed_control(
    double control_envelope_ns, double public_admission_control_ns,
    double queue_staging_control_ns, double poll_control_ns, double receive_rearm_control_ns,
    double command_control_ns, double owner_residual_ns, double request_control_ns,
    double completion_control_ns, double event_and_callback_control_ns,
    cnet_benchmark_fixed_control_attribution *out_attribution) {
  const double leaves[] = {public_admission_control_ns, queue_staging_control_ns, poll_control_ns,
                           receive_rearm_control_ns, command_control_ns, owner_residual_ns,
                           request_control_ns, completion_control_ns, event_and_callback_control_ns};
  double accounted = 0.0;

  if (out_attribution == NULL) return SALTS_EINVAL;
  if (!isfinite(control_envelope_ns) || control_envelope_ns <= 0.0) return SALTS_ERANGE;
  for (size_t index = 0u; index < sizeof(leaves) / sizeof(leaves[0]); ++index) {
    if (!isfinite(leaves[index]) || leaves[index] < 0.0) return SALTS_ERANGE;
    accounted += leaves[index];
    if (!isfinite(accounted) || accounted > control_envelope_ns) return SALTS_ERANGE;
  }

  *out_attribution = (cnet_benchmark_fixed_control_attribution){
      control_envelope_ns, accounted, control_envelope_ns - accounted};
  return SALTS_OK;
}
