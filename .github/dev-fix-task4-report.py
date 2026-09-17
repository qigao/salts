from pathlib import Path

path = Path('cnet/benchmarks/cnet_io_benchmark.c')
text = path.read_text()
old = '''static int io_bench_print_retained_comparison(const io_bench_series *retained_buffer,
                                              const io_bench_series *retained_slice,
                                              size_t count) {
  printf("\nTCP CNet retained buffer versus retained slice\n");
  printf("Retained-slice zero-copy is established by pointer identity and zero command "
         "payload-copy counters. Timing compares retained-view overhead only; it does not claim "
         "TLS ciphertext or kernel/network-stack zero-copy.\n");
  printf("mem_slice construction/release and guard-buffer preparation occur outside the timed "
         "send-admission interval.\n");
  printf("| payload | buffer admit ns | MAD ns | slice admit ns | MAD ns | slice vs buffer p50 "
         "median +/- MAD | p95 median +/- MAD | rate median +/- MAD |\n");
  printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    cnet_benchmark_summary p50_delta = {0};
    cnet_benchmark_summary p95_delta = {0};
    cnet_benchmark_summary rate_delta = {0};
    int status = io_bench_paired_delta(&retained_buffer[index], &retained_slice[index],
                                       IO_BENCH_METRIC_P50, &p50_delta);
    if (status == SALTS_OK)
      status = io_bench_paired_delta(&retained_buffer[index], &retained_slice[index],
                                     IO_BENCH_METRIC_P95, &p95_delta);
    if (status == SALTS_OK)
      status = io_bench_paired_delta(&retained_buffer[index], &retained_slice[index],
                                     IO_BENCH_METRIC_RATE, &rate_delta);
    if (status != SALTS_OK) return status;
    printf("| %zu KiB | %.1f | %.1f | %.1f | %.1f | %+.2f%% +/- %.2fpp | %+.2f%% +/- "
           "%.2fpp | %+.2f%% +/- %.2fpp |\n",
           retained_buffer[index].payload_size / 1024u,
           retained_buffer[index].cnet_send_admission_ns.median,
           retained_buffer[index].cnet_send_admission_ns.mad,
           retained_slice[index].cnet_send_admission_ns.median,
           retained_slice[index].cnet_send_admission_ns.mad,
           p50_delta.median, p50_delta.mad, p95_delta.median, p95_delta.mad,
           rate_delta.median, rate_delta.mad);
  }
  return SALTS_OK;
}
'''
# The malformed source contains literal newlines inside C string literals. Build the
# exact broken text by translating escaped newline markers in the canonical block.
broken = old.replace('\\n', '\n')
if text.count(broken) != 1:
    raise SystemExit(f'expected exactly one malformed retained report block, found {text.count(broken)}')
path.write_text(text.replace(broken, old, 1))
