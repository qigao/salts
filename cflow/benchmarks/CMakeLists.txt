cmake_add_benchmark(cflow_direct_benchmark
  SOURCES
    cflow_direct_benchmark.c
    cflow_direct_benchmark_consume.c
    cflow_graph_path_benchmark.c
  LIBS TurboUtils::CFlow TurboUtils::STL TurboUtils::TinyTest
  FOLDER "cflow/benchmarks")

cmake_add_benchmark(cflow_parallel_reduce_benchmark
  SOURCES cflow_parallel_reduce_benchmark.c
  LIBS TurboUtils::CFlow TurboUtils::TinyTest
  FOLDER "cflow/benchmarks")

set_target_properties(cflow_direct_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

set_target_properties(cflow_parallel_reduce_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
