cmake_add_benchmark(cstl_standard_benchmark
  SOURCES cstl_standard_benchmark.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/benchmarks")

set_target_properties(cstl_standard_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
