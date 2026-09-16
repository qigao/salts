from pathlib import Path

path = Path("cnet/benchmarks/cnet_io_benchmark.c")
text = path.read_text()

def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected one match, got {count}: {old[:160]!r}")
    text = text.replace(old, new, 1)

replace_once(
'''typedef enum io_bench_driver {
  IO_BENCH_LIBUV = 0,
  IO_BENCH_NATIVE_IO,
  IO_BENCH_NATIVE_IO_COROUTINE,
  IO_BENCH_CNET
} io_bench_driver;
''',
'''typedef enum io_bench_driver {
  IO_BENCH_LIBUV = 0,
  IO_BENCH_NATIVE_IO,
  IO_BENCH_NATIVE_IO_COROUTINE,
  IO_BENCH_CNET,
  IO_BENCH_CNET_DIRECT_DISPATCH
} io_bench_driver;
''')

replace_once(
'''static int io_bench_cnet_init(io_bench_cnet *fixture, io_bench_protocol protocol,
                              const struct sockaddr_in *address,
                              native_io_backend_kind backend_kind) {
''',
'''static int io_bench_cnet_init(io_bench_cnet *fixture, io_bench_protocol protocol,
                              const struct sockaddr_in *address,
                              native_io_backend_kind backend_kind, bool direct_dispatch) {
''')

replace_once(
'''  status = cnet_client_init(&fixture->client, &config);
  if (status != SALTS_OK) return status;
  (void)snprintf(uri, sizeof(uri), "%s://127.0.0.1:%u", protocol == IO_BENCH_TCP ? "tcp" : "udp",
''',
'''  status = cnet_client_init(&fixture->client, &config);
  if (status == SALTS_OK && direct_dispatch)
    status = cnet_client_set_diagnostic_direct_dispatch(&fixture->client, true);
  if (status != SALTS_OK) return status;
  (void)snprintf(uri, sizeof(uri), "%s://127.0.0.1:%u", protocol == IO_BENCH_TCP ? "tcp" : "udp",
''')

replace_once(
'''    else
      status = io_bench_cnet_init(&fixture->cnet, protocol, &fixture->server.address, backend_kind);
  }
  if (status == SALTS_OK && driver == IO_BENCH_CNET)
''',
'''    else
      status = io_bench_cnet_init(&fixture->cnet, protocol, &fixture->server.address, backend_kind,
                                  driver == IO_BENCH_CNET_DIRECT_DISPATCH);
  }
  if (status == SALTS_OK &&
      (driver == IO_BENCH_CNET || driver == IO_BENCH_CNET_DIRECT_DISPATCH))
''')

replace_once(
'''static const char *io_bench_driver_name(io_bench_driver driver) {
  if (driver == IO_BENCH_LIBUV) return "libuv";
  if (driver == IO_BENCH_NATIVE_IO) return "NativeIO direct";
  return driver == IO_BENCH_NATIVE_IO_COROUTINE ? "NativeIO coroutine" : "CNet";
}
''',
'''static const char *io_bench_driver_name(io_bench_driver driver) {
  if (driver == IO_BENCH_LIBUV) return "libuv";
  if (driver == IO_BENCH_NATIVE_IO) return "NativeIO direct";
  if (driver == IO_BENCH_NATIVE_IO_COROUTINE) return "NativeIO coroutine";
  return driver == IO_BENCH_CNET_DIRECT_DISPATCH ? "CNet direct-dispatch" : "CNet";
}
''')

marker = '''static int io_bench_run_row(io_bench_protocol protocol, size_t payload, size_t row,
'''
insert = r'''static int io_bench_run_cnet_dispatch_ab(size_t payload, io_bench_series *current,
                                             io_bench_series *direct,
                                             native_io_backend_kind backend_kind) {
  for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
    const bool current_first = (repeat & 1u) == 0u;
    const io_bench_driver first_driver =
        current_first ? IO_BENCH_CNET : IO_BENCH_CNET_DIRECT_DISPATCH;
    const io_bench_driver second_driver =
        current_first ? IO_BENCH_CNET_DIRECT_DISPATCH : IO_BENCH_CNET;
    io_bench_result *first = current_first ? &current->runs[repeat] : &direct->runs[repeat];
    io_bench_result *second = current_first ? &direct->runs[repeat] : &current->runs[repeat];
    int status = io_bench_run(IO_BENCH_TCP, first_driver, payload, false, backend_kind, first);
    if (status == SALTS_OK)
      status = io_bench_run(IO_BENCH_TCP, second_driver, payload, false, backend_kind, second);
    if (status != SALTS_OK) return status;
  }
  {
    int status = io_bench_series_finalize_comparison(current);
    if (status == SALTS_OK) status = io_bench_series_finalize_comparison(direct);
    return status;
  }
}

static int io_bench_print_cnet_dispatch_ab(const io_bench_series *current,
                                            const io_bench_series *direct) {
  cnet_benchmark_summary p50 = {0};
  cnet_benchmark_summary p95 = {0};
  cnet_benchmark_summary rate = {0};
  int status = io_bench_paired_delta(current, direct, IO_BENCH_METRIC_P50, &p50);
  if (status == SALTS_OK)
    status = io_bench_paired_delta(current, direct, IO_BENCH_METRIC_P95, &p95);
  if (status == SALTS_OK)
    status = io_bench_paired_delta(current, direct, IO_BENCH_METRIC_RATE, &rate);
  if (status != SALTS_OK) return status;

  printf("\nTCP 1 KiB CNet dispatcher-bypass A/B paired deltas\n");
  printf("Negative latency means direct-dispatch is faster; positive rate means higher throughput.\n");
  printf("| metric | direct-dispatch vs current median +/- MAD |\n");
  printf("| --- | ---: |\n");
  printf("| p50 | %+.2f%% +/- %.2fpp |\n", p50.median, p50.mad);
  printf("| p95 | %+.2f%% +/- %.2fpp |\n", p95.median, p95.mad);
  printf("| rate | %+.2f%% +/- %.2fpp |\n", rate.median, rate.mad);

  printf("\nTCP 1 KiB CNet dispatcher-bypass A/B raw repeats\n");
  printf("| repeat | current p50 us | direct p50 us | current p95 us | direct p95 us | current RT/s | direct RT/s |\n");
  printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
    const io_bench_result *a = &current->runs[repeat];
    const io_bench_result *b = &direct->runs[repeat];
    printf("| %zu | %.3f | %.3f | %.3f | %.3f | %.0f | %.0f |\n", repeat + 1u,
           (double)a->p50_ns / 1000.0, (double)b->p50_ns / 1000.0,
           (double)a->p95_ns / 1000.0, (double)b->p95_ns / 1000.0,
           io_bench_rate(a), io_bench_rate(b));
  }
  return SALTS_OK;
}

'''
if text.count(marker) != 1:
    raise SystemExit(f"expected one run-row marker, got {text.count(marker)}")
text = text.replace(marker, insert + marker, 1)

replace_once(
'''    io_bench_series cnet_tcp[sizeof(IO_BENCH_TCP_PAYLOADS) / sizeof(IO_BENCH_TCP_PAYLOADS[0])] = {
        0};
''',
'''    io_bench_series cnet_tcp[sizeof(IO_BENCH_TCP_PAYLOADS) / sizeof(IO_BENCH_TCP_PAYLOADS[0])] = {
        0};
    io_bench_series cnet_dispatch_current = {0};
    io_bench_series cnet_dispatch_direct = {0};
''')

replace_once(
'''      check_equal(io_bench_print_null_control("UDP", direct_aa_a_udp, direct_aa_b_udp, udp_count),
                  SALTS_OK);
    }

    check_equal(io_bench_print_latency("TCP", "p50", libuv_tcp, native_tcp, coroutine_tcp, cnet_tcp,
''',
'''      check_equal(io_bench_print_null_control("UDP", direct_aa_a_udp, direct_aa_b_udp, udp_count),
                  SALTS_OK);
      check_equal(io_bench_run_cnet_dispatch_ab(IO_BENCH_TCP_PAYLOADS[0], &cnet_dispatch_current,
                                                &cnet_dispatch_direct, backend.kind),
                  SALTS_OK);
      check_equal(io_bench_print_cnet_dispatch_ab(&cnet_dispatch_current, &cnet_dispatch_direct),
                  SALTS_OK);
    }

    check_equal(io_bench_print_latency("TCP", "p50", libuv_tcp, native_tcp, coroutine_tcp, cnet_tcp,
''')

path.write_text(text)
