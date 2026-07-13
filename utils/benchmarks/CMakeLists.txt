# Common module benchmarks

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
