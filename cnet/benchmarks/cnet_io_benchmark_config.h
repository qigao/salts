#ifndef CNET_IO_BENCHMARK_CONFIG_H
#define CNET_IO_BENCHMARK_CONFIG_H

#include <salts/native_io.h>

#include <stdbool.h>

typedef struct cnet_io_benchmark_backend {
  native_io_backend_kind kind;
  const char *name;
} cnet_io_benchmark_backend;

typedef struct cnet_io_benchmark_protocol {
  bool native_direct_aa_control;
} cnet_io_benchmark_protocol;

int cnet_io_benchmark_select_backend(const char *requested, cnet_io_benchmark_backend *selected);
int cnet_io_benchmark_protocol_default(cnet_io_benchmark_protocol *protocol);

#endif
