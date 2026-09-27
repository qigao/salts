cmake_add_benchmark(cstl_semantic_zero_benchmark
  SOURCES cstl_semantic_zero_benchmark.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/benchmarks")

set_target_properties(cstl_semantic_zero_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
