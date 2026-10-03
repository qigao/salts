if(NOT (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
        ((APPLE OR CMAKE_SYSTEM_NAME MATCHES "^(FreeBSD|OpenBSD|NetBSD|DragonFly)$")
         AND CMAKE_SIZEOF_VOID_P EQUAL 8)))
  return()
endif()

find_package(libuv CONFIG REQUIRED)
if(TARGET libuv::uv_a)
  set(CNET_LIBUV_TARGET libuv::uv_a)
elseif(TARGET libuv::uv)
  set(CNET_LIBUV_TARGET libuv::uv)
else()
  message(FATAL_ERROR "libuv package did not provide libuv::uv_a or libuv::uv")
endif()

cmake_add_benchmark(cnet_io_benchmark
  SOURCES cnet_io_benchmark.c cnet_io_benchmark_config.c cnet_benchmark_stats.c
  LIBS cnet_profile Salts::TinyTest ${CNET_LIBUV_TARGET}
  FOLDER "cnet/benchmarks")
target_include_directories(cnet_io_benchmark PRIVATE ../src)
set_target_properties(cnet_io_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_benchmark(cnet_tls_sg_benchmark
  SOURCES cnet_tls_sg_benchmark.c
  LIBS cnet_profile Salts::TinyTest
  FOLDER "cnet/benchmarks")
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures/p256-test-root-cert.pem"
     CNET_TLS_BENCH_CA)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures/p256-test-server-cert.pem"
     CNET_TLS_BENCH_CERT)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures/p256-test-server-key.pem"
     CNET_TLS_BENCH_KEY)
target_compile_definitions(cnet_tls_sg_benchmark PRIVATE
  "CNET_TLS_BENCH_CA=\"${CNET_TLS_BENCH_CA}\""
  "CNET_TLS_BENCH_CERT=\"${CNET_TLS_BENCH_CERT}\""
  "CNET_TLS_BENCH_KEY=\"${CNET_TLS_BENCH_KEY}\"")
unset(CNET_TLS_BENCH_CA)
unset(CNET_TLS_BENCH_CERT)
unset(CNET_TLS_BENCH_KEY)
target_include_directories(cnet_tls_sg_benchmark PRIVATE ../src)
set_target_properties(cnet_tls_sg_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_benchmark(cnet_owner_lifecycle_compare
  SOURCES cnet_owner_lifecycle_compare.c cnet_benchmark_stats.c
  LIBS Salts::Platform Salts::Core Salts::NativeIO
  FOLDER "cnet/benchmarks")
target_include_directories(cnet_owner_lifecycle_compare PRIVATE ../include ../src)
if(UNIX AND NOT APPLE)
  target_link_libraries(cnet_owner_lifecycle_compare PRIVATE dl)
endif()
add_test(NAME cnet_owner_lifecycle_compare_self_test
  COMMAND cnet_owner_lifecycle_compare --self-test)
add_dependencies(cnet_io_benchmark cnet_owner_lifecycle_compare)


if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cmake_add_benchmark(cnet_scaling_benchmark
    SOURCES cnet_scaling_benchmark.c cnet_io_benchmark_config.c cnet_benchmark_stats.c
    LIBS cnet_profile Salts::Platform Salts::NativeIO
    FOLDER "cnet/benchmarks")
  cmake_add_benchmark(cnet_owner_parallel_benchmark
    SOURCES cnet_owner_parallel_benchmark.c cnet_io_benchmark_config.c
    LIBS Salts::CNet Salts::Platform
    FOLDER "cnet/benchmarks")
  target_include_directories(cnet_owner_parallel_benchmark PRIVATE ../src)
  set_target_properties(cnet_owner_parallel_benchmark PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

  cmake_add_benchmark(cnet_shards_parallel_benchmark
    SOURCES cnet_shards_parallel_benchmark.c cnet_io_benchmark_config.c
    LIBS cnet_profile Salts::Platform
    FOLDER "cnet/benchmarks")
  target_include_directories(cnet_shards_parallel_benchmark PRIVATE ../src)
  set_target_properties(cnet_shards_parallel_benchmark PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

  cmake_add_benchmark(cnet_central_callback_benchmark
    SOURCES cnet_central_callback_benchmark.c cnet_io_benchmark_config.c
    LIBS cnet_profile Salts::Platform
    FOLDER "cnet/benchmarks")
  cmake_add_benchmark(cnet_external_owner_parallel_benchmark
    SOURCES cnet_external_owner_parallel_benchmark.c cnet_io_benchmark_config.c
    LIBS Salts::CNet Salts::Platform Salts::NativeIO
    FOLDER "cnet/benchmarks")
  target_include_directories(cnet_external_owner_parallel_benchmark PRIVATE ../src)
  set_target_properties(cnet_external_owner_parallel_benchmark PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

  target_include_directories(cnet_central_callback_benchmark PRIVATE ../src)
  set_target_properties(cnet_central_callback_benchmark PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

  target_include_directories(cnet_scaling_benchmark PRIVATE ../src)
  set_target_properties(cnet_scaling_benchmark PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
endif()
