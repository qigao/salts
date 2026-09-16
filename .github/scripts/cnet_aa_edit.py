from pathlib import Path

path = Path("cnet/benchmarks/cnet_io_benchmark.c")
text = path.read_text()


def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected exactly one match, got {count}: {old[:100]!r}")
    text = text.replace(old, new, 1)


old_finalize = '''static int io_bench_series_finalize(io_bench_series *series, io_bench_driver driver) {
  double values[IO_BENCH_REPLICATES];
  int status;

  series->payload_size = series->runs[0].payload_size;
  status = io_bench_series_summarize(series, IO_BENCH_METRIC_P50, &series->p50_ns);
  if (status == SALTS_OK)
    status = io_bench_series_summarize(series, IO_BENCH_METRIC_P95, &series->p95_ns);
  if (status == SALTS_OK)
    status = io_bench_series_summarize(series, IO_BENCH_METRIC_RATE, &series->rate_per_second);
  if (status != SALTS_OK) return status;
'''
new_finalize = '''static int io_bench_series_finalize_comparison(io_bench_series *series) {
  int status;
  series->payload_size = series->runs[0].payload_size;
  status = io_bench_series_summarize(series, IO_BENCH_METRIC_P50, &series->p50_ns);
  if (status == SALTS_OK)
    status = io_bench_series_summarize(series, IO_BENCH_METRIC_P95, &series->p95_ns);
  if (status == SALTS_OK)
    status = io_bench_series_summarize(series, IO_BENCH_METRIC_RATE, &series->rate_per_second);
  return status;
}

static int io_bench_series_finalize(io_bench_series *series, io_bench_driver driver) {
  double values[IO_BENCH_REPLICATES];
  int status = io_bench_series_finalize_comparison(series);
  if (status != SALTS_OK) return status;
'''
replace_once(old_finalize, new_finalize)

paired_marker = '''static int io_bench_print_latency(const char *protocol, const char *percentile,
'''
null_control = '''static int io_bench_print_null_control(const char *protocol,
                                       const io_bench_series *direct_a,
                                       const io_bench_series *direct_b, size_t count) {
  printf("\\n%s NativeIO direct A/A null-control paired deltas\\n", protocol);
  printf("| payload | p50 B vs A median +/- MAD | p95 B vs A median +/- MAD | "
         "rate B vs A median +/- MAD |\\n");
  printf("| ---: | ---: | ---: | ---: |\\n");
  for (size_t index = 0u; index < count; ++index) {
    cnet_benchmark_summary p50 = {0};
    cnet_benchmark_summary p95 = {0};
    cnet_benchmark_summary rate = {0};
    int status = io_bench_paired_delta(&direct_a[index], &direct_b[index],
                                       IO_BENCH_METRIC_P50, &p50);
    if (status == SALTS_OK)
      status = io_bench_paired_delta(&direct_a[index], &direct_b[index],
                                     IO_BENCH_METRIC_P95, &p95);
    if (status == SALTS_OK)
      status = io_bench_paired_delta(&direct_a[index], &direct_b[index],
                                     IO_BENCH_METRIC_RATE, &rate);
    if (status != SALTS_OK) return status;
    printf("| %zu KiB | %+.2f%% +/- %.2fpp | %+.2f%% +/- %.2fpp | %+.2f%% +/- %.2fpp |\\n",
           direct_a[index].payload_size / 1024u, p50.median, p50.mad, p95.median, p95.mad,
           rate.median, rate.mad);
  }

  printf("\\n%s NativeIO direct A/A null-control raw repeats\\n", protocol);
  printf("| payload | repeat | direct A p50 us | direct B p50 us | direct A p95 us | "
         "direct B p95 us | direct A RT/s | direct B RT/s |\\n");
  printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\\n");
  for (size_t index = 0u; index < count; ++index) {
    for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
      const io_bench_result *a = &direct_a[index].runs[repeat];
      const io_bench_result *b = &direct_b[index].runs[repeat];
      printf("| %zu KiB | %zu | %.3f | %.3f | %.3f | %.3f | %.0f | %.0f |\\n",
             direct_a[index].payload_size / 1024u, repeat + 1u, (double)a->p50_ns / 1000.0,
             (double)b->p50_ns / 1000.0, (double)a->p95_ns / 1000.0,
             (double)b->p95_ns / 1000.0, io_bench_rate(a), io_bench_rate(b));
    }
  }
  return SALTS_OK;
}

'''
replace_once(paired_marker, null_control + paired_marker)

run_row_marker = '''static int io_bench_run_row(io_bench_protocol protocol, size_t payload, size_t row,
'''
aa_runner = '''static int io_bench_run_direct_aa_row(io_bench_protocol protocol, size_t payload, size_t row,
                                      io_bench_series *direct_a, io_bench_series *direct_b,
                                      native_io_backend_kind backend_kind) {
  for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
    io_bench_series *first = ((row + repeat) & 1u) == 0u ? direct_a : direct_b;
    io_bench_series *second = first == direct_a ? direct_b : direct_a;
    int status = io_bench_run(protocol, IO_BENCH_NATIVE_IO, payload, false, backend_kind,
                              &first->runs[repeat]);
    if (status == SALTS_OK)
      status = io_bench_run(protocol, IO_BENCH_NATIVE_IO, payload, false, backend_kind,
                            &second->runs[repeat]);
    if (status != SALTS_OK) return status;
  }
  {
    int status = io_bench_series_finalize_comparison(direct_a);
    if (status == SALTS_OK) status = io_bench_series_finalize_comparison(direct_b);
    return status;
  }
}

'''
replace_once(run_row_marker, aa_runner + run_row_marker)

spec_start = '''  it("compares persistent TCP and UDP clients against one common echo peer") {
    cnet_io_benchmark_backend backend = {0};
    const char *requested_backend = getenv("CNET_IO_BENCHMARK_BACKEND");
    const int backend_status = cnet_io_benchmark_select_backend(requested_backend, &backend);
'''
spec_new = '''  it("compares persistent TCP and UDP clients against one common echo peer") {
    cnet_io_benchmark_backend backend = {0};
    cnet_io_benchmark_protocol benchmark_protocol = {0};
    const char *requested_backend = getenv("CNET_IO_BENCHMARK_BACKEND");
    const int backend_status = cnet_io_benchmark_select_backend(requested_backend, &backend);
    const int protocol_status = cnet_io_benchmark_protocol_default(&benchmark_protocol);
'''
replace_once(spec_start, spec_new)

native_tcp_decl = '''    io_bench_series native_tcp[sizeof(IO_BENCH_TCP_PAYLOADS) / sizeof(IO_BENCH_TCP_PAYLOADS[0])] = {
        0};
'''
replace_once(native_tcp_decl, native_tcp_decl + '''    io_bench_series direct_aa_a_tcp[sizeof(IO_BENCH_TCP_PAYLOADS) /
                                          sizeof(IO_BENCH_TCP_PAYLOADS[0])] = {0};
    io_bench_series direct_aa_b_tcp[sizeof(IO_BENCH_TCP_PAYLOADS) /
                                          sizeof(IO_BENCH_TCP_PAYLOADS[0])] = {0};
''')

native_udp_decl = '''    io_bench_series native_udp[sizeof(IO_BENCH_UDP_PAYLOADS) / sizeof(IO_BENCH_UDP_PAYLOADS[0])] = {
        0};
'''
replace_once(native_udp_decl, native_udp_decl + '''    io_bench_series direct_aa_a_udp[sizeof(IO_BENCH_UDP_PAYLOADS) /
                                          sizeof(IO_BENCH_UDP_PAYLOADS[0])] = {0};
    io_bench_series direct_aa_b_udp[sizeof(IO_BENCH_UDP_PAYLOADS) /
                                          sizeof(IO_BENCH_UDP_PAYLOADS[0])] = {0};
''')

status_block = '''    check_equal(backend_status, SALTS_OK);
    if (backend_status != SALTS_OK) return;
'''
replace_once(status_block, '''    check_equal(backend_status, SALTS_OK);
    check_equal(protocol_status, SALTS_OK);
    if (backend_status != SALTS_OK || protocol_status != SALTS_OK) return;
''')

stage_line = '''    printf("Stage clocks run in separate diagnostic repeats and do not instrument the "
           "comparison rows.\\n");
'''
replace_once(stage_line, stage_line + '''    if (benchmark_protocol.native_direct_aa_control)
      printf("IOCP null control: NativeIO direct A/B uses independent fresh fixtures with paired "
             "alternating order and the same warmup/measurement/statistics pipeline.\\n");
''')

run_loops = '''    for (size_t index = 0u; index < udp_count; ++index)
      check_equal(io_bench_run_row(IO_BENCH_UDP, IO_BENCH_UDP_PAYLOADS[index], index,
                                   &libuv_udp[index], &native_udp[index], &coroutine_udp[index],
                                   &cnet_udp[index], backend.kind),
                  SALTS_OK);

'''
replace_once(run_loops, run_loops + '''    if (benchmark_protocol.native_direct_aa_control) {
      for (size_t index = 0u; index < tcp_count; ++index)
        check_equal(io_bench_run_direct_aa_row(IO_BENCH_TCP, IO_BENCH_TCP_PAYLOADS[index], index,
                                               &direct_aa_a_tcp[index], &direct_aa_b_tcp[index],
                                               backend.kind),
                    SALTS_OK);
      for (size_t index = 0u; index < udp_count; ++index)
        check_equal(io_bench_run_direct_aa_row(IO_BENCH_UDP, IO_BENCH_UDP_PAYLOADS[index], index,
                                               &direct_aa_a_udp[index], &direct_aa_b_udp[index],
                                               backend.kind),
                    SALTS_OK);
      check_equal(io_bench_print_null_control("TCP", direct_aa_a_tcp, direct_aa_b_tcp, tcp_count),
                  SALTS_OK);
      check_equal(io_bench_print_null_control("UDP", direct_aa_a_udp, direct_aa_b_udp, udp_count),
                  SALTS_OK);
    }

''')

path.write_text(text)
