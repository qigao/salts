set(BENCHMARK_SOURCES benchmark_json_parser.c benchmark_query.c
                    benchmark_json_whitespace.c benchmark_regex.c benchmark_filter.c)

foreach(benchmark_source ${BENCHMARK_SOURCES})
  get_filename_component(benchmark_name ${benchmark_source} NAME_WE)
  cmake_add_benchmark(${benchmark_name}
    SOURCES ${benchmark_source}
    FOLDER "parser/${TARGET_NAME}/benchmarks"
    LIBS json_parser tinytest
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
             ${CMAKE_CURRENT_SOURCE_DIR}/../parser
             ${SIMDE_INCLUDE_DIRS})
endforeach()

# benchmark_regex exercises the internal SIMDe first-byte scanner in
# utils/src/re_scan.h (the regex engine now lives in TurboUtils::Core).
if(TARGET benchmark_regex)
  target_include_directories(benchmark_regex PRIVATE ${PROJECT_SOURCE_DIR}/utils/src)
endif()
