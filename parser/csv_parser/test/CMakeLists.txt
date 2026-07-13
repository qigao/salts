set(TEST_TARGET test_csv_parser)

add_executable(${TEST_TARGET} test_csv_parser.c)

target_link_libraries(${TEST_TARGET} PRIVATE csv_parser TurboUtils::TinyTest)

add_test(NAME ${TEST_TARGET} COMMAND ${TEST_TARGET})

cmake_config_target(${TEST_TARGET} FOLDER "parser/csv_parser")

add_executable(test_csv_stream_processor test_csv_stream_processor.c)
target_link_libraries(test_csv_stream_processor
                      PRIVATE TurboUtils::TinyTest csv_parser ${TARGET_NAME_LIB})
add_test(NAME test_csv_stream_processor COMMAND test_csv_stream_processor)
cmake_config_target(test_csv_stream_processor FOLDER "parser/csv_parser")

add_executable(test_dsv_filter test_dsv_filter.c)
target_link_libraries(test_dsv_filter PRIVATE TurboUtils::TinyTest csv_parser
                                              ${TARGET_NAME_LIB})
add_test(NAME test_dsv_filter COMMAND test_dsv_filter)
cmake_config_target(test_dsv_filter FOLDER "parser/csv_parser")

if(BUILD_BENCHMARKS)
  add_executable(benchmark_csv_random_access benchmark_csv_random_access.c)
  target_link_libraries(benchmark_csv_random_access PRIVATE TurboUtils::TinyTest csv_parser)
  cmake_config_target(benchmark_csv_random_access
                      FOLDER "parser/csv_parser/benchmarks")

  add_executable(benchmark_csv_simde benchmark_csv_simde.c)
  target_include_directories(benchmark_csv_simde PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../parser
                                                          ${SIMDE_INCLUDE_DIRS})
  target_link_libraries(benchmark_csv_simde PRIVATE TurboUtils::TinyTest csv_parser)
  cmake_config_target(benchmark_csv_simde
                      FOLDER "parser/csv_parser/benchmarks")
endif()
