cmake_add_benchmark(cmeta_fastpath_benchmark
  SOURCES cmeta_fastpath_benchmark.c cmeta_fastpath_benchmark_target.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/benchmarks")
set_target_properties(cmeta_fastpath_benchmark PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(BUILD_TESTS)
  add_test(NAME cmeta_fastpath_benchmark COMMAND cmeta_fastpath_benchmark)
endif()

cmake_add_benchmark(cmeta_trace_benchmark
  SOURCES cmeta_trace_benchmark.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/benchmarks")
set_target_properties(cmeta_trace_benchmark PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(BUILD_TESTS)
  add_test(NAME cmeta_trace_benchmark COMMAND cmeta_trace_benchmark)
endif()
