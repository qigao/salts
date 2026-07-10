set(BENCHMARK_LIBS TurboUtils::Mustache TurboUtils::JsonParser TurboUtils::TinyTest)
set(BENCHMARK_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_executable(test_mustache_bench test_mustache_bench.c)
target_link_libraries(test_mustache_bench PRIVATE ${BENCHMARK_LIBS})
target_include_directories(test_mustache_bench PRIVATE ${BENCHMARK_INCLUDES})
cmake_config_target(test_mustache_bench NO_INSTALL FOLDER "mustache/benchmarks")

if(BUILD_TESTS)
  add_test(NAME test_mustache_bench COMMAND test_mustache_bench)
  set_tests_properties(test_mustache_bench PROPERTIES LABELS "bench")
endif()
