#include "cnet_benchmark_stats.h"
#include "tinytest.h"

#include <salts/error_codes.h>

spec("CNet benchmark paired statistics") {
  it("keeps unmeasured wall time instead of forcing diagnostic closure") {
    const cnet_benchmark_phase_sample sample = {1000u, 100u, 500u, 80u};
    cnet_benchmark_phase_budget budget = {0};
    check_equal(cnet_benchmark_phase_decompose(&sample, 2u, &budget), SALTS_OK);
    check_equal(budget.wall_ns, 500.0);
    check_equal(budget.start_ns, 50.0);
    check_equal(budget.drive_ns, 250.0);
    check_equal(budget.check_ns, 40.0);
    check_equal(budget.remainder_ns, 160.0);
  }

  it("rejects overlapping or overflowing diagnostic spans without clamping") {
    const cnet_benchmark_phase_sample overlap = {100u, 60u, 50u, 0u};
    const cnet_benchmark_phase_sample overflow = {UINT64_MAX, UINT64_MAX, 1u, 0u};
    cnet_benchmark_phase_budget budget = {0};
    check_equal(cnet_benchmark_phase_decompose(&overlap, 1u, &budget), SALTS_ERANGE);
    check_equal(cnet_benchmark_phase_decompose(&overflow, 1u, &budget), SALTS_ERANGE);
    check_equal(cnet_benchmark_phase_decompose(&overlap, 0u, &budget), SALTS_EINVAL);
    check_equal(cnet_benchmark_phase_decompose(NULL, 1u, &budget), SALTS_EINVAL);
    check_equal(cnet_benchmark_phase_decompose(&overlap, 1u, NULL), SALTS_EINVAL);
  }

  it("reports the median and MAD without pooling independent runs") {
    const double values[] = {30.0, 10.0, 200.0, 20.0, 5.0};
    cnet_benchmark_summary summary = {0};

    check_equal(cnet_benchmark_summarize(values, 5u, &summary), SALTS_OK);
    check_equal(summary.median, 20.0);
    check_equal(summary.mad, 10.0);
  }

  it("computes deltas from matched baseline and candidate runs") {
    const double baseline[] = {100.0, 200.0, 400.0, 800.0, 1600.0};
    const double candidate[] = {105.0, 220.0, 480.0, 1040.0, 4800.0};
    cnet_benchmark_summary summary = {0};

    check_equal(cnet_benchmark_summarize_paired_delta(baseline, candidate, 5u, &summary), SALTS_OK);
    check_equal(summary.median, 20.0);
    check_equal(summary.mad, 10.0);
  }

  it("closes one diagnostic CNet round trip into exclusive fixed, native, copy, and benchmark work") {
    const cnet_benchmark_fixed_control_sample sample = {
        .round_trips = 1u,
        .send_admit_ns = 1000u,
        .queue_publish_ns = 600u,
        .payload_copy_ns = 300u,
        .client_poll_ns = 10000u,
        .owner_drive_ns = 9000u,
        .request_lifecycle_ns = 2000u,
        .request_start_ns = 1500u,
        .request_resubmit_ns = 200u,
        .observe_ns = 3000u,
        .request_completion_ns = 2500u,
        .event_publish_ns = 1900u,
        .dispatcher_prepare_ns = 200u,
        .dispatcher_invoke_ns = 1500u,
        .dispatcher_observer_ns = 1100u,
        .dispatcher_release_ns = 100u,
        .benchmark_callback_ns = 800u,
        .benchmark_payload_check_ns = 500u};
    cnet_benchmark_fixed_control_attribution attribution = {0};

    check_equal(cnet_benchmark_attribute_fixed_control(&sample, &attribution), SALTS_OK);
    check_equal(attribution.total_budget_ns, 11000.0);
    check_equal(attribution.send_public_control_ns, 400.0);
    check_equal(attribution.queue_control_ns, 300.0);
    check_equal(attribution.payload_copy_ns, 300.0);
    check_equal(attribution.client_poll_wrapper_ns, 1000.0);
    check_equal(attribution.owner_control_ns, 1300.0);
    check_equal(attribution.request_control_ns, 500.0);
    check_equal(attribution.native_request_start_ns, 1500.0);
    check_equal(attribution.native_request_resubmit_ns, 200.0);
    check_equal(attribution.native_observe_ns, 3000.0);
    check_equal(attribution.completion_control_ns, 600.0);
    check_equal(attribution.event_publish_residual_ns, 200.0);
    check_equal(attribution.dispatcher_prepare_ns, 200.0);
    check_equal(attribution.dispatcher_invoke_framework_ns, 300.0);
    check_equal(attribution.client_observer_control_ns, 300.0);
    check_equal(attribution.dispatcher_release_ns, 100.0);
    check_equal(attribution.benchmark_payload_check_ns, 500.0);
    check_equal(attribution.benchmark_callback_residual_ns, 300.0);
    check_equal(attribution.fixed_control_total_ns, 5200.0);
    check_equal(attribution.shared_native_total_ns, 4700.0);
    check_equal(attribution.benchmark_work_total_ns, 800.0);
    check_equal(attribution.closure_residual_ns, 0.0);
  }

  it("rejects inconsistent fixed-control nesting instead of hiding it in closure residual") {
    cnet_benchmark_fixed_control_sample sample = {
        .round_trips = 1u,
        .send_admit_ns = 100u,
        .queue_publish_ns = 90u,
        .payload_copy_ns = 80u,
        .client_poll_ns = 1000u,
        .owner_drive_ns = 900u,
        .request_lifecycle_ns = 200u,
        .request_start_ns = 100u,
        .observe_ns = 500u,
        .request_completion_ns = 400u,
        .event_publish_ns = 300u,
        .dispatcher_prepare_ns = 50u,
        .dispatcher_invoke_ns = 200u,
        .dispatcher_observer_ns = 150u,
        .dispatcher_release_ns = 25u,
        .benchmark_callback_ns = 100u,
        .benchmark_payload_check_ns = 50u};
    cnet_benchmark_fixed_control_attribution attribution = {0};

    sample.queue_publish_ns = 110u;
    check_equal(cnet_benchmark_attribute_fixed_control(&sample, &attribution), SALTS_ERANGE);
    sample.queue_publish_ns = 90u;
    sample.owner_drive_ns = 600u;
    check_equal(cnet_benchmark_attribute_fixed_control(&sample, &attribution), SALTS_ERANGE);
    sample.owner_drive_ns = 900u;
    sample.dispatcher_invoke_ns = 160u;
    check_equal(cnet_benchmark_attribute_fixed_control(&sample, &attribution), SALTS_ERANGE);
    sample.dispatcher_invoke_ns = 200u;
    sample.benchmark_callback_ns = 160u;
    check_equal(cnet_benchmark_attribute_fixed_control(&sample, &attribution), SALTS_ERANGE);
  }

  it("rejects invalid or non-finite samples") {
    const double invalid[] = {1.0, 0.0};
    cnet_benchmark_summary summary = {0};

    check_equal(cnet_benchmark_summarize(NULL, 1u, &summary), SALTS_EINVAL);
    check_equal(cnet_benchmark_summarize(invalid, 2u, &summary), SALTS_ERANGE);
    {
      cnet_benchmark_fixed_control_sample sample = {0};
      cnet_benchmark_fixed_control_attribution attribution = {0};
      check_equal(cnet_benchmark_attribute_fixed_control(NULL, &attribution), SALTS_EINVAL);
      check_equal(cnet_benchmark_attribute_fixed_control(&sample, &attribution), SALTS_EINVAL);
      sample.round_trips = 1u;
      check_equal(cnet_benchmark_attribute_fixed_control(&sample, NULL), SALTS_EINVAL);
    }
  }
}
