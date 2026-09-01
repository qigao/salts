#include "cnet_benchmark_stats.h"

#include <turbo/error_codes.h>

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

  if (values == NULL || out_summary == NULL || count == 0u) return TURBO_EINVAL;
  if (count > SIZE_MAX / sizeof(*scratch)) return TURBO_ERANGE;
  for (size_t index = 0u; index < count; ++index) {
    if (!isfinite(values[index]) || (require_positive && values[index] <= 0.0)) return TURBO_ERANGE;
  }
  scratch = (double *)malloc(count * sizeof(*scratch));
  if (scratch == NULL) return TURBO_ENOMEM;
  for (size_t index = 0u; index < count; ++index)
    scratch[index] = values[index];
  qsort(scratch, count, sizeof(*scratch), cnet_benchmark_double_compare);
  median = cnet_benchmark_median(scratch, count);
  for (size_t index = 0u; index < count; ++index)
    scratch[index] = fabs(values[index] - median);
  qsort(scratch, count, sizeof(*scratch), cnet_benchmark_double_compare);
  *out_summary = (cnet_benchmark_summary){median, cnet_benchmark_median(scratch, count)};
  free(scratch);
  return TURBO_OK;
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
    return TURBO_EINVAL;
  if (count > SIZE_MAX / sizeof(*deltas)) return TURBO_ERANGE;
  deltas = (double *)malloc(count * sizeof(*deltas));
  if (deltas == NULL) return TURBO_ENOMEM;
  for (size_t index = 0u; index < count; ++index) {
    if (!isfinite(baseline[index]) || baseline[index] <= 0.0 || !isfinite(candidate[index]) ||
        candidate[index] <= 0.0) {
      free(deltas);
      return TURBO_ERANGE;
    }
    deltas[index] = (candidate[index] / baseline[index] - 1.0) * 100.0;
  }
  status = cnet_benchmark_summarize_impl(deltas, count, false, out_summary);
  free(deltas);
  return status;
}
