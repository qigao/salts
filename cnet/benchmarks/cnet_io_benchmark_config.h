#ifndef CNET_IO_BENCHMARK_CONFIG_H
#define CNET_IO_BENCHMARK_CONFIG_H

#include <salts/native_io.h>

#include <stdbool.h>

typedef struct cnet_io_benchmark_backend {
  native_io_backend_kind kind;
  const char *name;
} cnet_io_benchmark_backend;

typedef enum io_bench_driver {
  IO_BENCH_LIBUV = 0,
  IO_BENCH_NATIVE_IO,
  IO_BENCH_NATIVE_IO_COROUTINE,
  IO_BENCH_CNET,
  IO_BENCH_DRIVER_COUNT
} io_bench_driver;

enum { CNET_IO_BENCHMARK_MAX_PAYLOAD = 65536, CNET_IO_BENCHMARK_MAX_DATAGRAM = 8192 };

typedef struct cnet_io_benchmark_trace {
  bool enabled;
  bool udp;
  io_bench_driver driver;
  size_t payload_size;
} cnet_io_benchmark_trace;

int cnet_io_benchmark_select_backend(const char *requested, cnet_io_benchmark_backend *selected);
/* NULL disables tracing; otherwise libuv|native|coroutine|cnet:tcp|udp:bytes.
 * Invalid input leaves the output unchanged. Trace results are not scores. */
int cnet_io_benchmark_select_trace(const char *requested, cnet_io_benchmark_trace *selected);
/* NULL keeps the baseline; "1" selects the separate retained-send experiment.
 * Trace and send-comparison modes are mutually exclusive. */
int cnet_io_benchmark_select_send_comparison(const char *requested, bool trace_enabled,
                                             bool *selected);

#endif
