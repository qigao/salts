cmake_add_benchmark(container_standard_benchmark
  SOURCES container_standard_benchmark.c
  LIBS TurboUtils::Container TurboUtils::TinyTest
  FOLDER "container/benchmarks")

set_target_properties(container_standard_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
