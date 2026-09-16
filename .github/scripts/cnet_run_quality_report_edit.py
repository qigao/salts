from pathlib import Path

stats_path = Path("cnet/benchmarks/cnet_benchmark_stats.c")
stats = stats_path.read_text()

stats_marker = '''  return SALTS_OK;
}

int cnet_benchmark_attribute_send(uint64_t send_admit_ns, uint64_t send_admit_calls,
'''
stats_insert = '''  return SALTS_OK;
}

const char *cnet_benchmark_run_quality_label(cnet_benchmark_run_quality_state state) {
  if (state == CNET_BENCHMARK_RUN_QUALIFIED) return "qualified for performance decisions";
  if (state == CNET_BENCHMARK_RUN_NOISE_LIMITED)
    return "noise-limited; do not use for optimization decisions";
  return "unknown";
}

int cnet_benchmark_attribute_send(uint64_t send_admit_ns, uint64_t send_admit_calls,
'''
if stats.count(stats_marker) != 1:
    raise SystemExit(f"stats marker count={stats.count(stats_marker)}")
stats_path.write_text(stats.replace(stats_marker, stats_insert, 1))

bench_path = Path("cnet/benchmarks/cnet_io_benchmark.c")
bench = bench_path.read_text()

function_marker = '''static int io_bench_print_latency(const char *protocol, const char *percentile,
'''
quality_function = '''static int io_bench_print_run_quality(const io_bench_series *direct_a,
                                      const io_bench_series *direct_b,
                                      const io_bench_series *native,
                                      const io_bench_series *cnet) {
  cnet_benchmark_summary null_p50 = {0};
  cnet_benchmark_summary baseline_p50 = {0};
  cnet_benchmark_run_quality quality = {0};
  int status = io_bench_paired_delta(direct_a, direct_b, IO_BENCH_METRIC_P50, &null_p50);
  if (status == SALTS_OK)
    status = io_bench_paired_delta(native, cnet, IO_BENCH_METRIC_P50, &baseline_p50);
  if (status == SALTS_OK)
    status = cnet_benchmark_assess_run_quality(&null_p50, &baseline_p50, &quality);
  if (status != SALTS_OK) return status;

  printf("\\nIOCP TCP 1 KiB benchmark run quality\\n");
  printf("NativeIO A/A p50: %+.2f%% +/- %.2fpp\\n", null_p50.median, null_p50.mad);
  printf("CNet vs NativeIO direct p50: %+.2f%% +/- %.2fpp\\n", baseline_p50.median,
         baseline_p50.mad);
  printf("A/A noise envelope: %.2fpp\\n", quality.noise_envelope_pp);
  printf("Baseline lower bound: %.2fpp\\n", quality.baseline_lower_bound_pp);
  printf("RUN QUALITY: %s\\n", cnet_benchmark_run_quality_label(quality.state));
  return SALTS_OK;
}

'''
if bench.count(function_marker) != 1:
    raise SystemExit(f"function marker count={bench.count(function_marker)}")
bench = bench.replace(function_marker, quality_function + function_marker, 1)

call_marker = '''      check_equal(io_bench_print_null_control("UDP", direct_aa_a_udp, direct_aa_b_udp, udp_count),
                  SALTS_OK);
    }
'''
call_insert = '''      check_equal(io_bench_print_null_control("UDP", direct_aa_a_udp, direct_aa_b_udp, udp_count),
                  SALTS_OK);
      check_equal(io_bench_print_run_quality(&direct_aa_a_tcp[0], &direct_aa_b_tcp[0],
                                             &native_tcp[0], &cnet_tcp[0]),
                  SALTS_OK);
    }
'''
if bench.count(call_marker) != 1:
    raise SystemExit(f"call marker count={bench.count(call_marker)}")
bench_path.write_text(bench.replace(call_marker, call_insert, 1))
