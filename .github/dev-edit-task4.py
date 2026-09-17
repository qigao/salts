from pathlib import Path

path = Path('cnet/benchmarks/cnet_io_benchmark.c')
text = path.read_text()

def replace_once(old, new, label):
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly one match, found {count}')
    text = text.replace(old, new, 1)

replace_once(
'''typedef enum io_bench_protocol { IO_BENCH_TCP = 0, IO_BENCH_UDP } io_bench_protocol;
typedef enum io_bench_driver {
''',
'''typedef enum io_bench_protocol { IO_BENCH_TCP = 0, IO_BENCH_UDP } io_bench_protocol;
typedef enum io_bench_cnet_retained_mode {
  IO_BENCH_CNET_RETAINED_NONE = 0,
  IO_BENCH_CNET_RETAINED_BUFFER,
  IO_BENCH_CNET_RETAINED_SLICE
} io_bench_cnet_retained_mode;
typedef enum io_bench_driver {
''',
'retained mode enum')

replace_once(
'''  size_t poll_calls;
  size_t callback_calls;
  bool measuring;
} io_bench_cnet;
''',
'''  size_t poll_calls;
  size_t callback_calls;
  mem_pool_t send_pool;
  mem_buffer_t *send_buffer;
  mem_slice_t send_slice;
  io_bench_cnet_retained_mode retained_mode;
  bool send_pool_initialized;
  bool measuring;
} io_bench_cnet;
''',
'cnet retained fixture fields')

replace_once(
'''static int io_bench_cnet_init(io_bench_cnet *fixture, io_bench_protocol protocol,
                              const struct sockaddr_in *address,
                              native_io_backend_kind backend_kind) {
''',
'''static int io_bench_cnet_prepare_retained(io_bench_cnet *fixture,
                                            io_bench_cnet_retained_mode mode,
                                            const unsigned char *payload,
                                            size_t payload_size) {
  enum { IO_BENCH_SLICE_GUARD_BYTES = 16 };
  unsigned char *data;
  size_t storage_size;

  if (fixture == NULL || payload == NULL || payload_size == 0u) return SALTS_EINVAL;
  if (mode == IO_BENCH_CNET_RETAINED_NONE) return SALTS_OK;
  if (fixture->protocol != IO_BENCH_TCP || fixture->send_pool_initialized ||
      fixture->send_buffer != NULL)
    return SALTS_EINVAL;
  storage_size = payload_size;
  if (mode == IO_BENCH_CNET_RETAINED_SLICE) {
    if (payload_size > SIZE_MAX - 2u * IO_BENCH_SLICE_GUARD_BYTES) return SALTS_ERANGE;
    storage_size = payload_size + 2u * IO_BENCH_SLICE_GUARD_BYTES;
  } else if (mode != IO_BENCH_CNET_RETAINED_BUFFER) {
    return SALTS_EINVAL;
  }
  if (mem_init(&fixture->send_pool, 0u) != 0) return SALTS_ENOMEM;
  fixture->send_pool_initialized = true;
  fixture->send_buffer = mem_get_buffer(&fixture->send_pool, storage_size);
  if (fixture->send_buffer == NULL) return SALTS_ENOMEM;
  data = (unsigned char *)mem_buffer_data(fixture->send_buffer);
  if (mode == IO_BENCH_CNET_RETAINED_SLICE) {
    memset(data, 0xa5, storage_size);
    memcpy(data + IO_BENCH_SLICE_GUARD_BYTES, payload, payload_size);
  } else {
    memcpy(data, payload, payload_size);
  }
  mem_set_used(fixture->send_buffer, storage_size);
  fixture->retained_mode = mode;
  if (mode == IO_BENCH_CNET_RETAINED_SLICE) {
    fixture->send_slice =
        mem_slice(fixture->send_buffer, IO_BENCH_SLICE_GUARD_BYTES, payload_size);
    if (fixture->send_slice.buffer != fixture->send_buffer ||
        fixture->send_slice.data !=
            (char *)mem_buffer_data(fixture->send_buffer) + IO_BENCH_SLICE_GUARD_BYTES ||
        fixture->send_slice.length != payload_size)
      return SALTS_EPROTO;
  }
  return SALTS_OK;
}

static int io_bench_cnet_init(io_bench_cnet *fixture, io_bench_protocol protocol,
                              const struct sockaddr_in *address,
                              native_io_backend_kind backend_kind) {
''',
'cnet retained preparation')

replace_once(
'''  if (status == SALTS_OK) {
    const uint64_t started = fixture->measuring ? salts_hrtime() : 0u;
    status = cnet_send(&fixture->client, fixture->connection, sent, length);
    if (fixture->measuring) {
''',
'''  if (status == SALTS_OK) {
    const uint64_t started = fixture->measuring ? salts_hrtime() : 0u;
    if (fixture->retained_mode == IO_BENCH_CNET_RETAINED_BUFFER)
      status = cnet_send_buffer(&fixture->client, fixture->connection, fixture->send_buffer);
    else if (fixture->retained_mode == IO_BENCH_CNET_RETAINED_SLICE)
      status = cnet_send_slice(&fixture->client, fixture->connection, &fixture->send_slice);
    else
      status = cnet_send(&fixture->client, fixture->connection, sent, length);
    if (fixture->measuring) {
''',
'cnet send mode selection')

replace_once(
'''static int io_bench_cnet_destroy(io_bench_cnet *fixture) {
  int status = SALTS_OK;
  if (fixture->client.impl != NULL) {
    status = cnet_close(&fixture->client, fixture->connection);
    if (status == SALTS_OK) status = io_bench_wait_cnet(fixture, &fixture->terminal, 1);
    if (status == SALTS_OK || status == SALTS_EALREADY || status == SALTS_ENOENT)
      status = cnet_client_stop(&fixture->client, IO_BENCH_TIMEOUT_MS);
    if (status == SALTS_OK) status = cnet_client_destroy(&fixture->client);
  }
  return status;
}
''',
'''static int io_bench_cnet_destroy(io_bench_cnet *fixture) {
  int status = SALTS_OK;
  if (fixture->client.impl != NULL) {
    status = cnet_close(&fixture->client, fixture->connection);
    if (status == SALTS_OK) status = io_bench_wait_cnet(fixture, &fixture->terminal, 1);
    if (status == SALTS_OK || status == SALTS_EALREADY || status == SALTS_ENOENT)
      status = cnet_client_stop(&fixture->client, IO_BENCH_TIMEOUT_MS);
    if (status == SALTS_OK) status = cnet_client_destroy(&fixture->client);
  }
  if (fixture->send_slice.buffer != NULL) mem_slice_release(&fixture->send_slice);
  if (fixture->send_buffer != NULL) {
    mem_buffer_release(fixture->send_buffer);
    fixture->send_buffer = NULL;
  }
  if (fixture->send_pool_initialized) {
    mem_destroy(&fixture->send_pool);
    fixture->send_pool_initialized = false;
  }
  fixture->retained_mode = IO_BENCH_CNET_RETAINED_NONE;
  return status;
}
''',
'cnet retained cleanup')

replace_once(
'''static int io_bench_run(io_bench_protocol protocol, io_bench_driver driver, size_t payload_size,
                        bool profile_stages, native_io_backend_kind backend_kind,
                        io_bench_result *result) {
''',
'''static int io_bench_run_mode(io_bench_protocol protocol, io_bench_driver driver,
                             size_t payload_size, bool profile_stages,
                             native_io_backend_kind backend_kind,
                             io_bench_cnet_retained_mode retained_mode,
                             io_bench_result *result) {
''',
'run mode signature')

replace_once(
'''  memset(sent, 0x5a, payload_size);
  phase = "warmup";
''',
'''  memset(sent, 0x5a, payload_size);
  if (retained_mode != IO_BENCH_CNET_RETAINED_NONE) {
    if (driver != IO_BENCH_CNET) {
      status = SALTS_EINVAL;
      goto cleanup;
    }
    status = io_bench_cnet_prepare_retained(&fixture.cnet, retained_mode, sent, payload_size);
    if (status != SALTS_OK) goto cleanup;
  }
  phase = "warmup";
''',
'run retained preparation')

replace_once(
'''    status = cnet_client_profile_take(&fixture.cnet.client, &result->cnet_profile);
    fixture.cnet.measuring = false;
    if (status != SALTS_OK) goto cleanup;
  }
  status = SALTS_OK;
''',
'''    status = cnet_client_profile_take(&fixture.cnet.client, &result->cnet_profile);
    fixture.cnet.measuring = false;
    if (status != SALTS_OK) goto cleanup;
    if (retained_mode != IO_BENCH_CNET_RETAINED_NONE &&
        (result->cnet_profile.owner.command_queue_payload_copy_calls != 0u ||
         result->cnet_profile.owner.command_queue_payload_copy_ns != 0u)) {
      status = SALTS_EPROTO;
      goto cleanup;
    }
  }
  status = SALTS_OK;
''',
'run retained zero copy proof')

replace_once(
'''  return status;
}

static double io_bench_rate(const io_bench_result *result) {
''',
'''  return status;
}

static int io_bench_run(io_bench_protocol protocol, io_bench_driver driver, size_t payload_size,
                        bool profile_stages, native_io_backend_kind backend_kind,
                        io_bench_result *result) {
  return io_bench_run_mode(protocol, driver, payload_size, profile_stages, backend_kind,
                           IO_BENCH_CNET_RETAINED_NONE, result);
}

static int io_bench_run_cnet_retained(io_bench_protocol protocol, size_t payload_size,
                                      bool profile_stages,
                                      native_io_backend_kind backend_kind,
                                      io_bench_cnet_retained_mode retained_mode,
                                      io_bench_result *result) {
  if (retained_mode != IO_BENCH_CNET_RETAINED_BUFFER &&
      retained_mode != IO_BENCH_CNET_RETAINED_SLICE)
    return SALTS_EINVAL;
  return io_bench_run_mode(protocol, IO_BENCH_CNET, payload_size, profile_stages, backend_kind,
                           retained_mode, result);
}

static double io_bench_rate(const io_bench_result *result) {
''',
'run wrappers')

replace_once(
'''static int io_bench_paired_delta(const io_bench_series *baseline, const io_bench_series *candidate,
                                 io_bench_metric metric, cnet_benchmark_summary *out_summary) {
''',
'''static int io_bench_retained_series_finalize(io_bench_series *series) {
  double admission_values[IO_BENCH_REPLICATES];
  int status = io_bench_series_finalize_comparison(series);
  if (status != SALTS_OK) return status;
  for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
    const io_bench_result *result = &series->stage_profile_runs[repeat];
    if (result->round_trips == 0u || result->cnet_send_admission_calls == 0u ||
        result->cnet_profile.owner.command_queue_payload_copy_calls != 0u ||
        result->cnet_profile.owner.command_queue_payload_copy_ns != 0u)
      return SALTS_EPROTO;
    admission_values[repeat] =
        io_bench_mean(result->cnet_send_admission_ns, result->cnet_send_admission_calls);
  }
  return cnet_benchmark_summarize(admission_values, IO_BENCH_REPLICATES,
                                  &series->cnet_send_admission_ns);
}

static int io_bench_paired_delta(const io_bench_series *baseline, const io_bench_series *candidate,
                                 io_bench_metric metric, cnet_benchmark_summary *out_summary) {
''',
'retained series finalize')

replace_once(
'''static int io_bench_run_direct_aa_row(io_bench_protocol protocol, size_t payload, size_t row,
''',
'''static int io_bench_run_retained_row(size_t payload, size_t row,
                                      io_bench_series *retained_buffer,
                                      io_bench_series *retained_slice,
                                      native_io_backend_kind backend_kind) {
  for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
    io_bench_series *first = ((row + repeat) & 1u) == 0u ? retained_buffer : retained_slice;
    io_bench_series *second = first == retained_buffer ? retained_slice : retained_buffer;
    const io_bench_cnet_retained_mode first_mode =
        first == retained_buffer ? IO_BENCH_CNET_RETAINED_BUFFER : IO_BENCH_CNET_RETAINED_SLICE;
    const io_bench_cnet_retained_mode second_mode =
        second == retained_buffer ? IO_BENCH_CNET_RETAINED_BUFFER : IO_BENCH_CNET_RETAINED_SLICE;
    int status = io_bench_run_cnet_retained(IO_BENCH_TCP, payload, false, backend_kind,
                                            first_mode, &first->runs[repeat]);
    if (status == SALTS_OK)
      status = io_bench_run_cnet_retained(IO_BENCH_TCP, payload, false, backend_kind,
                                          second_mode, &second->runs[repeat]);
    if (status != SALTS_OK) return status;
  }
  for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
    io_bench_series *first = ((row + repeat) & 1u) == 0u ? retained_slice : retained_buffer;
    io_bench_series *second = first == retained_buffer ? retained_slice : retained_buffer;
    const io_bench_cnet_retained_mode first_mode =
        first == retained_buffer ? IO_BENCH_CNET_RETAINED_BUFFER : IO_BENCH_CNET_RETAINED_SLICE;
    const io_bench_cnet_retained_mode second_mode =
        second == retained_buffer ? IO_BENCH_CNET_RETAINED_BUFFER : IO_BENCH_CNET_RETAINED_SLICE;
    int status = io_bench_run_cnet_retained(IO_BENCH_TCP, payload, true, backend_kind,
                                            first_mode, &first->stage_profile_runs[repeat]);
    if (status == SALTS_OK)
      status = io_bench_run_cnet_retained(IO_BENCH_TCP, payload, true, backend_kind,
                                          second_mode, &second->stage_profile_runs[repeat]);
    if (status != SALTS_OK) return status;
  }
  {
    int status = io_bench_retained_series_finalize(retained_buffer);
    if (status == SALTS_OK) status = io_bench_retained_series_finalize(retained_slice);
    return status;
  }
}

static int io_bench_print_retained_comparison(const io_bench_series *retained_buffer,
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

static int io_bench_run_direct_aa_row(io_bench_protocol protocol, size_t payload, size_t row,
''',
'retained paired row and report')

replace_once(
'''    io_bench_series cnet_tcp[sizeof(IO_BENCH_TCP_PAYLOADS) / sizeof(IO_BENCH_TCP_PAYLOADS[0])] = {
        0};
    io_bench_series libuv_udp[sizeof(IO_BENCH_UDP_PAYLOADS) / sizeof(IO_BENCH_UDP_PAYLOADS[0])] = {
''',
'''    io_bench_series cnet_tcp[sizeof(IO_BENCH_TCP_PAYLOADS) / sizeof(IO_BENCH_TCP_PAYLOADS[0])] = {
        0};
    io_bench_series retained_buffer_tcp[sizeof(IO_BENCH_TCP_PAYLOADS) /
                                              sizeof(IO_BENCH_TCP_PAYLOADS[0])] = {0};
    io_bench_series retained_slice_tcp[sizeof(IO_BENCH_TCP_PAYLOADS) /
                                             sizeof(IO_BENCH_TCP_PAYLOADS[0])] = {0};
    io_bench_series libuv_udp[sizeof(IO_BENCH_UDP_PAYLOADS) / sizeof(IO_BENCH_UDP_PAYLOADS[0])] = {
''',
'retained series declarations')

replace_once(
'''    for (size_t index = 0u; index < udp_count; ++index)
      check_equal(io_bench_run_row(IO_BENCH_UDP, IO_BENCH_UDP_PAYLOADS[index], index,
                                   &libuv_udp[index], &native_udp[index], &coroutine_udp[index],
                                   &cnet_udp[index], backend.kind),
                  SALTS_OK);

    if (benchmark_protocol.native_direct_aa_control) {
''',
'''    for (size_t index = 0u; index < udp_count; ++index)
      check_equal(io_bench_run_row(IO_BENCH_UDP, IO_BENCH_UDP_PAYLOADS[index], index,
                                   &libuv_udp[index], &native_udp[index], &coroutine_udp[index],
                                   &cnet_udp[index], backend.kind),
                  SALTS_OK);
    for (size_t index = 0u; index < tcp_count; ++index)
      check_equal(io_bench_run_retained_row(IO_BENCH_TCP_PAYLOADS[index], index,
                                            &retained_buffer_tcp[index],
                                            &retained_slice_tcp[index], backend.kind),
                  SALTS_OK);

    if (benchmark_protocol.native_direct_aa_control) {
''',
'run retained rows')

replace_once(
'''    io_bench_print_cnet_stages("TCP", cnet_tcp, tcp_count);
    check_equal(io_bench_print_cnet_fixed_control("TCP", cnet_tcp, tcp_count), SALTS_OK);
    check_equal(io_bench_print_latency("UDP", "p50", libuv_udp, native_udp, coroutine_udp, cnet_udp,
''',
'''    io_bench_print_cnet_stages("TCP", cnet_tcp, tcp_count);
    check_equal(io_bench_print_cnet_fixed_control("TCP", cnet_tcp, tcp_count), SALTS_OK);
    check_equal(io_bench_print_retained_comparison(retained_buffer_tcp, retained_slice_tcp,
                                                   tcp_count),
                SALTS_OK);
    check_equal(io_bench_print_latency("UDP", "p50", libuv_udp, native_udp, coroutine_udp, cnet_udp,
''',
'print retained comparison')

path.write_text(text)
