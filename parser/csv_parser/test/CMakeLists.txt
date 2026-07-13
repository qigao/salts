cmake_add_test(test_csv_parser
  SOURCES test_csv_parser.c
  LIBS csv_parser TurboUtils::TinyTest
  FOLDER "parser/csv_parser")

cmake_add_test(test_csv_stream_processor
  SOURCES test_csv_stream_processor.c
  LIBS TurboUtils::TinyTest csv_parser ${TARGET_NAME_LIB}
  FOLDER "parser/csv_parser")

cmake_add_test(test_dsv_filter
  SOURCES test_dsv_filter.c
  LIBS TurboUtils::TinyTest csv_parser ${TARGET_NAME_LIB}
  FOLDER "parser/csv_parser")

if(BUILD_BENCHMARKS)
  cmake_add_benchmark(benchmark_csv_random_access
    SOURCES benchmark_csv_random_access.c
    LIBS TurboUtils::TinyTest csv_parser
    FOLDER "parser/csv_parser/benchmarks")

  cmake_add_benchmark(benchmark_csv_simde
    SOURCES benchmark_csv_simde.c
    LIBS TurboUtils::TinyTest csv_parser
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../parser ${SIMDE_INCLUDE_DIRS}
    FOLDER "parser/csv_parser/benchmarks")
endif()
