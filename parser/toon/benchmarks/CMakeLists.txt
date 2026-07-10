add_executable(benchmark_toon benchmark_toon.c)
target_link_libraries(benchmark_toon PRIVATE toon)

if(UNIX)
  target_link_libraries(benchmark_toon PRIVATE m)
endif()

cmake_config_target(benchmark_toon NO_INSTALL FOLDER "parser/toon/benchmarks")

add_executable(benchmark_simd_scan benchmark_simd_scan.c)
target_include_directories(benchmark_simd_scan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..
                                                        ${SIMDE_INCLUDE_DIRS})
target_link_libraries(benchmark_simd_scan PRIVATE TurboUtils::TinyTest)
cmake_config_target(benchmark_simd_scan NO_INSTALL FOLDER "parser/toon/benchmarks")
