cmake_add_benchmark(thread_pool_lf_benchmark
  SOURCES thread_pool_lf_benchmark.c
  LIBS Salts::Concurrency Salts::Core Salts::TinyTest
  FOLDER "concurrency/benchmarks")
set_target_properties(thread_pool_lf_benchmark PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME thread_pool_lf_benchmark COMMAND thread_pool_lf_benchmark)
set_tests_properties(thread_pool_lf_benchmark PROPERTIES LABELS benchmark TIMEOUT 120)
