cmake_add_benchmark(turbostl_standard_benchmark
  SOURCES turbostl_standard_benchmark.c
  LIBS Rocida::STL Rocida::TinyTest
  FOLDER "turbostl/benchmarks")

set_target_properties(turbostl_standard_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
