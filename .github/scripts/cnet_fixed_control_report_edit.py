from pathlib import Path

path = Path("cnet/benchmarks/cnet_io_benchmark.c")
text = path.read_text()


def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected exactly one match, got {count}: {old[:120]!r}")
    text = text.replace(old, new, 1)


marker = '''static void io_bench_print_cnet_stages(const char *protocol, const io_bench_series *cnet,
                                       size_t count) {
'''
insert = r'''static int io_bench_fixed_control_attribution(
    const io_bench_result *result, cnet_benchmark_fixed_control_attribution *out_attribution) {
  const cnet_owner_profile *owner;
  cnet_benchmark_fixed_control_sample sample;

  if (result == NULL || out_attribution == NULL || result->round_trips == 0u) return SALTS_EINVAL;
  owner = &result->cnet_profile.owner;
  sample = (cnet_benchmark_fixed_control_sample){
      .round_trips = result->round_trips,
      .send_admit_ns = result->cnet_send_admission_ns,
      .queue_publish_ns = owner->command_queue_payload_publish_ns,
      .payload_copy_ns = owner->command_queue_payload_copy_ns,
      .client_poll_ns = result->cnet_profile.client_poll_ns,
      .owner_drive_ns = owner->owner_drive_ns,
      .request_lifecycle_ns = owner->request_lifecycle_ns,
      .request_start_ns = owner->request_start_ns,
      .observe_ns = owner->observe_ns,
      .request_completion_ns = owner->request_completion_ns,
      .event_publish_ns = owner->event_publish_ns,
      .dispatcher_prepare_ns = result->cnet_profile.dispatcher_prepare_ns,
      .dispatcher_invoke_ns = result->cnet_profile.dispatcher_invoke_ns,
      .dispatcher_observer_ns = result->cnet_profile.dispatcher_observer_ns,
      .dispatcher_release_ns = result->cnet_profile.dispatcher_release_ns,
      .benchmark_callback_ns = result->cnet_callback_ns,
      .benchmark_payload_check_ns = result->cnet_benchmark_payload_check_ns};
  return cnet_benchmark_attribute_fixed_control(&sample, out_attribution);
}

static int io_bench_print_cnet_fixed_control(const char *protocol, const io_bench_series *cnet,
                                             size_t count) {
  printf("\n%s CNet closed diagnostic budget per repeat\n", protocol);
  printf("All columns are exclusive after nested clocks are removed. Fixed + shared NativeIO + "
         "payload copy + benchmark work + closure residual = profiled send + client poll.\n");
  printf("| payload | repeat | total budget us | fixed control us | shared NativeIO us | payload "
         "copy us | benchmark work us | closure residual ns |\n");
  printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
      cnet_benchmark_fixed_control_attribution attribution = {0};
      int status = io_bench_fixed_control_attribution(&cnet[index].stage_profile_runs[repeat],
                                                      &attribution);
      if (status != SALTS_OK) return status;
      printf("| %zu KiB | %zu | %.3f | %.3f | %.3f | %.3f | %.3f | %.1f |\n",
             cnet[index].payload_size / 1024u, repeat + 1u, attribution.total_budget_ns / 1000.0,
             attribution.fixed_control_total_ns / 1000.0,
             attribution.shared_native_total_ns / 1000.0, attribution.payload_copy_ns / 1000.0,
             attribution.benchmark_work_total_ns / 1000.0, attribution.closure_residual_ns);
    }
  }

  printf("\n%s CNet exclusive fixed-control components per repeat\n", protocol);
  printf("| payload | repeat | send public ns | queue control ns | client poll wrapper ns | owner "
         "control ns | request control ns | completion control ns | event residual ns | "
         "dispatcher prepare ns | dispatcher framework ns | client observer ns | dispatcher "
         "release ns |\n");
  printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | "
         "---: |\n");
  for (size_t index = 0u; index < count; ++index) {
    for (size_t repeat = 0u; repeat < IO_BENCH_REPLICATES; ++repeat) {
      cnet_benchmark_fixed_control_attribution attribution = {0};
      int status = io_bench_fixed_control_attribution(&cnet[index].stage_profile_runs[repeat],
                                                      &attribution);
      if (status != SALTS_OK) return status;
      printf("| %zu KiB | %zu | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f | "
             "%.1f | %.1f | %.1f |\n",
             cnet[index].payload_size / 1024u, repeat + 1u,
             attribution.send_public_control_ns, attribution.queue_control_ns,
             attribution.client_poll_wrapper_ns, attribution.owner_control_ns,
             attribution.request_control_ns, attribution.completion_control_ns,
             attribution.event_publish_residual_ns, attribution.dispatcher_prepare_ns,
             attribution.dispatcher_invoke_framework_ns, attribution.client_observer_control_ns,
             attribution.dispatcher_release_ns);
    }
  }
  return SALTS_OK;
}

'''
replace_once(marker, insert + marker)

replace_once('''    io_bench_print_cnet_stages("TCP", cnet_tcp, tcp_count);
''', '''    io_bench_print_cnet_stages("TCP", cnet_tcp, tcp_count);
    check_equal(io_bench_print_cnet_fixed_control("TCP", cnet_tcp, tcp_count), SALTS_OK);
''')
replace_once('''    io_bench_print_cnet_stages("UDP", cnet_udp, udp_count);
''', '''    io_bench_print_cnet_stages("UDP", cnet_udp, udp_count);
    check_equal(io_bench_print_cnet_fixed_control("UDP", cnet_udp, udp_count), SALTS_OK);
''')

path.write_text(text)
