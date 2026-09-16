from pathlib import Path

path = Path("cnet/benchmarks/cnet_benchmark_stats.c")
text = path.read_text()
marker = '''int cnet_benchmark_attribute_send(uint64_t send_admit_ns, uint64_t send_admit_calls,
'''
insert = '''int cnet_benchmark_assess_run_quality(const cnet_benchmark_summary *null_p50,
                                      const cnet_benchmark_summary *baseline_p50,
                                      cnet_benchmark_run_quality *out_quality) {
  double noise_envelope_pp;
  double baseline_lower_bound_pp;

  if (null_p50 == NULL || baseline_p50 == NULL || out_quality == NULL) return SALTS_EINVAL;
  if (!isfinite(null_p50->median) || !isfinite(null_p50->mad) || null_p50->mad < 0.0 ||
      !isfinite(baseline_p50->median) || !isfinite(baseline_p50->mad) || baseline_p50->mad < 0.0)
    return SALTS_ERANGE;

  noise_envelope_pp = fabs(null_p50->median) + 3.0 * null_p50->mad;
  baseline_lower_bound_pp = baseline_p50->median - baseline_p50->mad;
  if (!isfinite(noise_envelope_pp) || !isfinite(baseline_lower_bound_pp)) return SALTS_ERANGE;

  *out_quality = (cnet_benchmark_run_quality){
      .state = baseline_p50->median > 0.0 && baseline_lower_bound_pp > noise_envelope_pp
                   ? CNET_BENCHMARK_RUN_QUALIFIED
                   : CNET_BENCHMARK_RUN_NOISE_LIMITED,
      .noise_envelope_pp = noise_envelope_pp,
      .baseline_lower_bound_pp = baseline_lower_bound_pp,
  };
  return SALTS_OK;
}

'''
if text.count(marker) != 1:
    raise SystemExit(f"expected one insertion marker, got {text.count(marker)}")
path.write_text(text.replace(marker, insert + marker, 1))
