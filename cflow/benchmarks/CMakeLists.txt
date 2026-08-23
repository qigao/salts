cmake_add_benchmark(cflow_direct_benchmark
  SOURCES
    cflow_direct_benchmark.c
    cflow_direct_benchmark_consume.c
  LIBS TurboUtils::CFlow TurboUtils::TinyTest
  FOLDER "cflow/benchmarks")

set_target_properties(cflow_direct_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
