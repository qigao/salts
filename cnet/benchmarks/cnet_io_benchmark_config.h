#ifndef CNET_IO_BENCHMARK_CONFIG_H
#define CNET_IO_BENCHMARK_CONFIG_H

#include <salts/native_io.h>

typedef struct cnet_io_benchmark_backend {
  native_io_backend_kind kind;
  const char *name;
} cnet_io_benchmark_backend;

int cnet_io_benchmark_select_backend(const char *requested, cnet_io_benchmark_backend *selected);

#endif
