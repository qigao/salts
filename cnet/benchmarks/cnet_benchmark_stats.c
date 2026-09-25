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

static bool cnet_benchmark_u64_add(uint64_t left, uint64_t right, uint64_t *out) {
  if (out == NULL || left > UINT64_MAX - right) return false;
  *out = left + right;
  return true;
}

static double cnet_benchmark_per_rt(uint64_t total_ns, size_t round_trips) {
  return (double)total_ns / (double)round_trips;
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

int cnet_benchmark_phase_decompose(const cnet_benchmark_phase_sample *sample, size_t round_trips,
                                  cnet_benchmark_phase_budget *out_budget) {
  uint64_t measured_ns;
  if (sample == NULL || out_budget == NULL || round_trips == 0u) return SALTS_EINVAL;
  if (!cnet_benchmark_u64_add(sample->start_ns, sample->drive_ns, &measured_ns) ||
      !cnet_benchmark_u64_add(measured_ns, sample->check_ns, &measured_ns) ||
      measured_ns > sample->wall_ns)
    return SALTS_ERANGE;
  *out_budget = (cnet_benchmark_phase_budget){
      .wall_ns = cnet_benchmark_per_rt(sample->wall_ns, round_trips),
      .start_ns = cnet_benchmark_per_rt(sample->start_ns, round_trips),
      .drive_ns = cnet_benchmark_per_rt(sample->drive_ns, round_trips),
      .check_ns = cnet_benchmark_per_rt(sample->check_ns, round_trips),
      .remainder_ns = cnet_benchmark_per_rt(sample->wall_ns - measured_ns, round_trips)};
  return SALTS_OK;
}

int cnet_benchmark_attribute_fixed_control(
    const cnet_benchmark_fixed_control_sample *sample,
    cnet_benchmark_fixed_control_attribution *out_attribution) {
  uint64_t owner_nested_ns;
  uint64_t dispatcher_nested_ns;
  uint64_t event_nested_ns;
  uint64_t total_budget_ns;
  uint64_t fixed_control_ns;
  uint64_t shared_native_ns;
  uint64_t benchmark_work_ns;
  uint64_t accounted_ns;

  uint64_t send_public_control_ns;
  uint64_t queue_control_ns;
  uint64_t client_poll_wrapper_ns;
  uint64_t owner_control_ns;
  uint64_t request_control_ns;
  uint64_t completion_control_ns;
  uint64_t event_publish_residual_ns;
  uint64_t dispatcher_invoke_framework_ns;
  uint64_t client_observer_control_ns;
  uint64_t benchmark_callback_residual_ns;

  if (sample == NULL || out_attribution == NULL || sample->round_trips == 0u)
    return SALTS_EINVAL;

  if (sample->send_admit_ns < sample->queue_publish_ns ||
      sample->queue_publish_ns < sample->payload_copy_ns ||
      sample->client_poll_ns < sample->owner_drive_ns ||
      sample->request_lifecycle_ns < sample->request_start_ns ||
      sample->request_completion_ns < sample->event_publish_ns ||
      sample->dispatcher_observer_ns < sample->benchmark_callback_ns ||
      sample->benchmark_callback_ns < sample->benchmark_payload_check_ns)
    return SALTS_ERANGE;

  if (!cnet_benchmark_u64_add(sample->request_lifecycle_ns, sample->request_resubmit_ns,
                              &owner_nested_ns) ||
      !cnet_benchmark_u64_add(owner_nested_ns, sample->observe_ns, &owner_nested_ns) ||
      !cnet_benchmark_u64_add(owner_nested_ns, sample->request_completion_ns,
                              &owner_nested_ns) ||
      sample->owner_drive_ns < owner_nested_ns)
    return SALTS_ERANGE;

  if (!cnet_benchmark_u64_add(sample->dispatcher_observer_ns, sample->dispatcher_release_ns,
                              &dispatcher_nested_ns) ||
      sample->dispatcher_invoke_ns < dispatcher_nested_ns)
    return SALTS_ERANGE;

  if (!cnet_benchmark_u64_add(sample->dispatcher_prepare_ns, sample->dispatcher_invoke_ns,
                              &event_nested_ns) ||
      sample->event_publish_ns < event_nested_ns)
    return SALTS_ERANGE;

  send_public_control_ns = sample->send_admit_ns - sample->queue_publish_ns;
  queue_control_ns = sample->queue_publish_ns - sample->payload_copy_ns;
  client_poll_wrapper_ns = sample->client_poll_ns - sample->owner_drive_ns;
  owner_control_ns = sample->owner_drive_ns - owner_nested_ns;
  request_control_ns = sample->request_lifecycle_ns - sample->request_start_ns;
  completion_control_ns = sample->request_completion_ns - sample->event_publish_ns;
  event_publish_residual_ns = sample->event_publish_ns - event_nested_ns;
  dispatcher_invoke_framework_ns = sample->dispatcher_invoke_ns - dispatcher_nested_ns;
  client_observer_control_ns = sample->dispatcher_observer_ns - sample->benchmark_callback_ns;
  benchmark_callback_residual_ns =
      sample->benchmark_callback_ns - sample->benchmark_payload_check_ns;

  fixed_control_ns = 0u;
#define CNET_FIXED_ADD(value)                                                        \
  do {                                                                               \
    uint64_t next_fixed = 0u;                                                        \
    if (!cnet_benchmark_u64_add(fixed_control_ns, (value), &next_fixed))            \
      return SALTS_ERANGE;                                                           \
    fixed_control_ns = next_fixed;                                                   \
  } while (0)
  CNET_FIXED_ADD(send_public_control_ns);
  CNET_FIXED_ADD(queue_control_ns);
  CNET_FIXED_ADD(client_poll_wrapper_ns);
  CNET_FIXED_ADD(owner_control_ns);
  CNET_FIXED_ADD(request_control_ns);
  CNET_FIXED_ADD(completion_control_ns);
  CNET_FIXED_ADD(event_publish_residual_ns);
  CNET_FIXED_ADD(sample->dispatcher_prepare_ns);
  CNET_FIXED_ADD(dispatcher_invoke_framework_ns);
  CNET_FIXED_ADD(client_observer_control_ns);
  CNET_FIXED_ADD(sample->dispatcher_release_ns);
#undef CNET_FIXED_ADD

  if (!cnet_benchmark_u64_add(sample->request_start_ns, sample->request_resubmit_ns,
                              &shared_native_ns) ||
      !cnet_benchmark_u64_add(shared_native_ns, sample->observe_ns, &shared_native_ns) ||
      !cnet_benchmark_u64_add(sample->benchmark_payload_check_ns,
                              benchmark_callback_residual_ns, &benchmark_work_ns) ||
      !cnet_benchmark_u64_add(sample->send_admit_ns, sample->client_poll_ns,
                              &total_budget_ns))
    return SALTS_ERANGE;

  accounted_ns = 0u;
  if (!cnet_benchmark_u64_add(fixed_control_ns, shared_native_ns, &accounted_ns) ||
      !cnet_benchmark_u64_add(accounted_ns, sample->payload_copy_ns, &accounted_ns) ||
      !cnet_benchmark_u64_add(accounted_ns, benchmark_work_ns, &accounted_ns) ||
      accounted_ns > total_budget_ns)
    return SALTS_ERANGE;

  *out_attribution = (cnet_benchmark_fixed_control_attribution){
      .total_budget_ns = cnet_benchmark_per_rt(total_budget_ns, sample->round_trips),
      .send_public_control_ns = cnet_benchmark_per_rt(send_public_control_ns, sample->round_trips),
      .queue_control_ns = cnet_benchmark_per_rt(queue_control_ns, sample->round_trips),
      .payload_copy_ns = cnet_benchmark_per_rt(sample->payload_copy_ns, sample->round_trips),
      .client_poll_wrapper_ns = cnet_benchmark_per_rt(client_poll_wrapper_ns, sample->round_trips),
      .owner_control_ns = cnet_benchmark_per_rt(owner_control_ns, sample->round_trips),
      .request_control_ns = cnet_benchmark_per_rt(request_control_ns, sample->round_trips),
      .native_request_start_ns = cnet_benchmark_per_rt(sample->request_start_ns, sample->round_trips),
      .native_request_resubmit_ns =
          cnet_benchmark_per_rt(sample->request_resubmit_ns, sample->round_trips),
      .native_observe_ns = cnet_benchmark_per_rt(sample->observe_ns, sample->round_trips),
      .completion_control_ns = cnet_benchmark_per_rt(completion_control_ns, sample->round_trips),
      .event_publish_residual_ns =
          cnet_benchmark_per_rt(event_publish_residual_ns, sample->round_trips),
      .dispatcher_prepare_ns =
          cnet_benchmark_per_rt(sample->dispatcher_prepare_ns, sample->round_trips),
      .dispatcher_invoke_framework_ns =
          cnet_benchmark_per_rt(dispatcher_invoke_framework_ns, sample->round_trips),
      .client_observer_control_ns =
          cnet_benchmark_per_rt(client_observer_control_ns, sample->round_trips),
      .dispatcher_release_ns =
          cnet_benchmark_per_rt(sample->dispatcher_release_ns, sample->round_trips),
      .benchmark_payload_check_ns =
          cnet_benchmark_per_rt(sample->benchmark_payload_check_ns, sample->round_trips),
      .benchmark_callback_residual_ns =
          cnet_benchmark_per_rt(benchmark_callback_residual_ns, sample->round_trips),
      .fixed_control_total_ns = cnet_benchmark_per_rt(fixed_control_ns, sample->round_trips),
      .shared_native_total_ns = cnet_benchmark_per_rt(shared_native_ns, sample->round_trips),
      .benchmark_work_total_ns = cnet_benchmark_per_rt(benchmark_work_ns, sample->round_trips),
      .closure_residual_ns =
          cnet_benchmark_per_rt(total_budget_ns - accounted_ns, sample->round_trips),
  };
  return SALTS_OK;
}
