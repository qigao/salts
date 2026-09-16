#include "cnet_benchmark_stats.h"
#include "tinytest.h"

#include <salts/error_codes.h>

spec("CNet benchmark paired statistics") {
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

  it("separates send admission into exclusive producer-side stages") {
    cnet_benchmark_send_attribution attribution = {0};

    check_equal(cnet_benchmark_attribute_send(1000u, 2u, 600u, 2u, 400u, 2u, &attribution),
                SALTS_OK);
    check_equal(attribution.send_admit_ns, 500.0);
    check_equal(attribution.public_control_ns, 200.0);
    check_equal(attribution.queue_publish_ns, 300.0);
    check_equal(attribution.queue_staging_control_ns, 100.0);
    check_equal(attribution.payload_copy_ns, 200.0);
  }

  it("rejects inconsistent send attribution samples") {
    cnet_benchmark_send_attribution attribution = {0};

    check_equal(cnet_benchmark_attribute_send(1000u, 2u, 600u, 1u, 400u, 2u, &attribution),
                SALTS_ERANGE);
    check_equal(cnet_benchmark_attribute_send(500u, 1u, 600u, 1u, 400u, 1u, &attribution),
                SALTS_ERANGE);
    check_equal(cnet_benchmark_attribute_send(1000u, 1u, 600u, 1u, 700u, 1u, &attribution),
                SALTS_ERANGE);
  }

  it("rejects invalid or non-finite samples") {
    const double invalid[] = {1.0, 0.0};
    cnet_benchmark_summary summary = {0};

    check_equal(cnet_benchmark_summarize(NULL, 1u, &summary), SALTS_EINVAL);
    check_equal(cnet_benchmark_summarize(invalid, 2u, &summary), SALTS_ERANGE);
    check_equal(cnet_benchmark_attribute_send(1u, 1u, 1u, 1u, 1u, 1u, NULL), SALTS_EINVAL);
    {
      cnet_benchmark_send_attribution attribution = {0};
      check_equal(cnet_benchmark_attribute_send(1u, 0u, 1u, 1u, 1u, 1u, &attribution),
                  SALTS_EINVAL);
    }
  }
}
