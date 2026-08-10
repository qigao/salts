# Common module benchmarks

find_package(unofficial-sqlite3 CONFIG REQUIRED)

cmake_add_source(BENCHMARK_SOURCES DIRS ${CMAKE_CURRENT_SOURCE_DIR}
                 PATTERNS "bench_*.c" "test_*_bench.c")

set(COMMON_LIBS TurboUtils::TinyTest TurboUtils::Core)
if(WIN32)
  list(APPEND COMMON_LIBS psapi iphlpapi userenv ws2_32)
endif()

foreach(benchmark_source ${BENCHMARK_SOURCES})
  get_filename_component(benchmark_name ${benchmark_source} NAME_WE)
  cmake_add_benchmark(${benchmark_name}
    SOURCES ${benchmark_source}
    FOLDER "utils/benchmarks"
    LIBS ${COMMON_LIBS} uri_parser)
endforeach()

foreach(tgt test_fmt_bench)
  if(TARGET ${tgt})
    target_link_libraries(${tgt} PRIVATE fmt_lexer)
  endif()
endforeach()

foreach(tgt test_str_bench)
  if(TARGET ${tgt})
    target_link_libraries(${tgt} PRIVATE TurboUtils::SDS)
  endif()
endforeach()

target_link_libraries(bench_sqlite_index_reference
                      PRIVATE unofficial::sqlite3::sqlite3)

target_link_libraries(bench_sqlite_csv_vdbe
                      PRIVATE unofficial::sqlite3::sqlite3 csv_parser)
target_include_directories(bench_sqlite_csv_vdbe
                           PRIVATE ${CMAKE_SOURCE_DIR}/parser/csv_parser/parser
                                   ${CMAKE_BINARY_DIR}/parser/csv_parser)
