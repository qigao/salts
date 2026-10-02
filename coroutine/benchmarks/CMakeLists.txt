cmake_add_benchmark(salts_coro_executor_benchmark
  SOURCES salts_coro_executor_benchmark.c
  LIBS Salts::Coroutine Salts::Platform Salts::TinyTest
  FOLDER "coroutine/benchmarks")

set_target_properties(salts_coro_executor_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_benchmark(salts_coro_executor_batch_benchmark
  SOURCES salts_coro_executor_batch_benchmark.c
  LIBS Salts::Coroutine Salts::Platform
  FOLDER "coroutine/benchmarks")

target_include_directories(salts_coro_executor_batch_benchmark PRIVATE
  ${PROJECT_SOURCE_DIR}/coroutine/src)

set_target_properties(salts_coro_executor_batch_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
