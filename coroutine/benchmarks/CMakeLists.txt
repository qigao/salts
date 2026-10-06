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


cmake_add_benchmark(salts_coro_executor_dequeue_batch_benchmark
  SOURCES salts_coro_executor_dequeue_batch_benchmark.c
  LIBS Salts::Coroutine Salts::Platform
  FOLDER "coroutine/benchmarks")

target_include_directories(salts_coro_executor_dequeue_batch_benchmark PRIVATE
  ${PROJECT_SOURCE_DIR}/coroutine/src)

set_target_properties(salts_coro_executor_dequeue_batch_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

foreach(benchmark IN ITEMS salts_coro_executor_benchmark
    salts_coro_executor_batch_benchmark salts_coro_executor_dequeue_batch_benchmark)
  add_test(NAME ${benchmark} COMMAND ${benchmark})
  set_tests_properties(${benchmark} PROPERTIES LABELS benchmark
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
endforeach()
