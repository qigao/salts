cmake_add_benchmark(cflow_direct_benchmark
  SOURCES
    cflow_direct_benchmark.c
    cflow_direct_benchmark_consume.c
    cflow_range_benchmark.c
    cflow_graph_path_benchmark.c
    cflow_branching_views.c
    cflow_branching_csr_benchmark.c
  LIBS TurboUtils::CFlow TurboUtils::STL TurboUtils::TinyTest
  FOLDER "cflow/benchmarks")

cmake_add_benchmark(cflow_parallel_reduce_benchmark
  SOURCES cflow_parallel_reduce_benchmark.c
  LIBS TurboUtils::CFlow TurboUtils::TinyTest
  FOLDER "cflow/benchmarks")

cmake_add_benchmark(cflow_machine_hierarchy_benchmark
  SOURCES cflow_machine_hierarchy_benchmark.c
  LIBS TurboUtils::CFlow TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "cflow/benchmarks")

cmake_add_benchmark(cflow_network_benchmark
  SOURCES cflow_network_benchmark.c
  LIBS TurboUtils::CFlow TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "cflow/benchmarks")

cmake_add_benchmark(cflow_io_source_benchmark
  SOURCES cflow_io_source_benchmark.c
  LIBS TurboUtils::CFlow TurboUtils::TinyTest
  FOLDER "cflow/benchmarks")

if(CFLOW_ENABLE_MINICORO)
  target_link_libraries(cflow_io_source_benchmark
    PRIVATE TurboUtils::CFlowMinicoro)
  target_compile_definitions(cflow_io_source_benchmark
    PRIVATE CFLOW_IO_SOURCE_BENCH_HAS_MINICORO=1)
endif()

if(WIN32)
  target_link_libraries(cflow_network_benchmark PRIVATE ws2_32 psapi)
  target_compile_definitions(cflow_network_benchmark
    PRIVATE _CRT_SECURE_NO_WARNINGS=1)
endif()

set_target_properties(cflow_direct_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

target_include_directories(cflow_direct_benchmark PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src)

set_target_properties(cflow_parallel_reduce_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

set_target_properties(cflow_machine_hierarchy_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

set_target_properties(cflow_network_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

set_target_properties(cflow_io_source_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
