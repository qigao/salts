#include <stddef.h>

volatile double cflow_direct_benchmark_value_sink;

void cflow_direct_benchmark_consume(const double *data, size_t count) {
  if (data != NULL && count != 0u) cflow_direct_benchmark_value_sink = data[count - 1u];
}
