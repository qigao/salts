cmake_add_benchmark(turbo_coro_executor_benchmark
  SOURCES turbo_coro_executor_benchmark.c
  LIBS Rocida::Coroutine Rocida::Platform Rocida::TinyTest
  FOLDER "coroutine/benchmarks")

set_target_properties(turbo_coro_executor_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
