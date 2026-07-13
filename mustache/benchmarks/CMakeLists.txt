set(BENCHMARK_LIBS TurboUtils::Mustache TurboUtils::JsonParser TurboUtils::TinyTest)
set(BENCHMARK_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src)

cmake_add_benchmark(test_mustache_bench
  SOURCES test_mustache_bench.c
  LIBS ${BENCHMARK_LIBS}
  INCLUDES ${BENCHMARK_INCLUDES}
  FOLDER "mustache/benchmarks")

if(BUILD_TESTS)
  add_test(NAME test_mustache_bench COMMAND test_mustache_bench)
  set_tests_properties(test_mustache_bench PROPERTIES LABELS "bench")
endif()
