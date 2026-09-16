#include <salts/error_codes.h>

#include "cnet_benchmark_stats.h"

#include <stdio.h>
#include <string.h>

int cnet_owner_compare_run(const char *baseline_path, const char *candidate_path,
                           const char *report_path);

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--self-test") == 0) {
    const double baseline[] = {100.0, 100.0, 100.0, 100.0, 100.0};
    const double candidate[] = {95.0, 96.0, 97.0, 98.0, 99.0};
    cnet_benchmark_summary summary = {0};
    if (cnet_benchmark_summarize_paired_delta(baseline, candidate, 5u, &summary) != SALTS_OK)
      return 1;
    return summary.median == -3.0 ? 0 : 1;
  }
  if (argc != 4) {
    fprintf(stderr,
            "usage: %s <baseline-cnet-dso> <candidate-cnet-dso> <report-path>\n",
            argc > 0 ? argv[0] : "cnet_owner_lifecycle_compare");
    return 2;
  }
  return cnet_owner_compare_run(argv[1], argv[2], argv[3]);
}
