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

cmake_add_benchmark(cnet_transport_owner_benchmark
  SOURCES cnet_transport_owner_benchmark.c cnet_io_benchmark_config.c
  LIBS cnet Salts::Core Salts::TinyTest ${CNET_LIBUV_TARGET}
  FOLDER "cnet/benchmarks")
add_test(NAME cnet_transport_owner_benchmark COMMAND cnet_transport_owner_benchmark)
set_tests_properties(cnet_transport_owner_benchmark PROPERTIES LABELS benchmark TIMEOUT 120)

cmake_add_benchmark(cnet_io_benchmark
  SOURCES cnet_io_benchmark.c cnet_io_benchmark_config.c cnet_benchmark_stats.c
  LIBS cnet_profile Salts::TinyTest ${CNET_LIBUV_TARGET}
  FOLDER "cnet/benchmarks")
target_include_directories(cnet_io_benchmark PRIVATE ../src)
set_target_properties(cnet_io_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

if(APPLE)
  # A separate Mach-O image is necessary to interpose kevent in both libuv and
  # NativeIO. Only this diagnostic executable loads it; main A/B stays intact.
  add_library(cnet_kqueue_trace SHARED cnet_kqueue_trace.c)
  target_link_libraries(cnet_kqueue_trace PRIVATE Salts::Platform)
  set_target_properties(cnet_kqueue_trace PROPERTIES FOLDER "cnet/benchmarks"
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  cmake_add_benchmark(cnet_io_benchmark_kqueue_trace
    SOURCES cnet_io_benchmark.c cnet_io_benchmark_config.c cnet_benchmark_stats.c
    LIBS cnet_profile Salts::TinyTest ${CNET_LIBUV_TARGET} cnet_kqueue_trace
    DEFS CNET_IO_BENCHMARK_KQUEUE_TRACE=1
    INCLUDES ../src
    FOLDER "cnet/benchmarks")
  set_target_properties(cnet_io_benchmark_kqueue_trace PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  add_test(NAME cnet_io_benchmark_kqueue_trace COMMAND cnet_io_benchmark_kqueue_trace)
  set_tests_properties(cnet_io_benchmark_kqueue_trace PROPERTIES LABELS "benchmark;trace")
  cmake_add_test(cnet_kqueue_trace_test
    SOURCES ../tests/cnet_kqueue_trace_test.c
    LIBS cnet_kqueue_trace Salts::TinyTest Salts::Platform
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
    FOLDER "cnet/tests")
  set_tests_properties(cnet_kqueue_trace_test PROPERTIES LABELS "benchmark;trace")
endif()

cmake_add_benchmark(cnet_tls_sg_benchmark
  SOURCES cnet_tls_sg_benchmark.c
  LIBS cnet_profile Salts::TinyTest
  FOLDER "cnet/benchmarks")
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures/ip-test-root-cert.pem"
     CNET_TLS_BENCH_CA)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures/ip-test-server-cert.pem"
     CNET_TLS_BENCH_CERT)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/../tests/fixtures/ip-test-server-key.pem"
     CNET_TLS_BENCH_KEY)
target_compile_definitions(cnet_tls_sg_benchmark PRIVATE
  "CNET_TLS_BENCH_CA=\"${CNET_TLS_BENCH_CA}\""
  "CNET_TLS_BENCH_CERT=\"${CNET_TLS_BENCH_CERT}\""
  "CNET_TLS_BENCH_KEY=\"${CNET_TLS_BENCH_KEY}\"")
target_include_directories(cnet_tls_sg_benchmark PRIVATE ../src)
set_target_properties(cnet_tls_sg_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cmake_add_benchmark(cnet_tls_provider_parallel_benchmark
    SOURCES cnet_tls_provider_parallel_benchmark.c
    LIBS cnet_profile Salts::Platform
    FOLDER "cnet/benchmarks")
  target_compile_definitions(cnet_tls_provider_parallel_benchmark PRIVATE
    "CNET_TLS_BENCH_CA=\"${CNET_TLS_BENCH_CA}\""
    "CNET_TLS_BENCH_CERT=\"${CNET_TLS_BENCH_CERT}\""
    "CNET_TLS_BENCH_KEY=\"${CNET_TLS_BENCH_KEY}\"")
  target_include_directories(cnet_tls_provider_parallel_benchmark PRIVATE ../src)
  set_target_properties(cnet_tls_provider_parallel_benchmark PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

  cmake_add_benchmark(cnet_tls_public_owner_benchmark
    SOURCES cnet_tls_public_owner_benchmark.c
    LIBS cnet_profile Salts::Platform Salts::Core
    FOLDER "cnet/benchmarks")
  target_compile_definitions(cnet_tls_public_owner_benchmark PRIVATE
    "CNET_TLS_BENCH_CA=\"${CNET_TLS_BENCH_CA}\""
    "CNET_TLS_BENCH_CERT=\"${CNET_TLS_BENCH_CERT}\""
    "CNET_TLS_BENCH_KEY=\"${CNET_TLS_BENCH_KEY}\"")
  target_include_directories(cnet_tls_public_owner_benchmark PRIVATE ../src)
  set_target_properties(cnet_tls_public_owner_benchmark PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
endif()

unset(CNET_TLS_BENCH_CA)
unset(CNET_TLS_BENCH_CERT)
unset(CNET_TLS_BENCH_KEY)

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

foreach(benchmark IN ITEMS cnet_io_benchmark cnet_tls_sg_benchmark
    cnet_scaling_benchmark cnet_owner_parallel_benchmark
    cnet_shards_parallel_benchmark cnet_central_callback_benchmark
    cnet_external_owner_parallel_benchmark)
  if(TARGET ${benchmark})
    add_test(NAME ${benchmark} COMMAND ${benchmark})
    set_tests_properties(${benchmark} PROPERTIES LABELS benchmark
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
  endif()
endforeach()

# Workloads and comparison DSOs belong to the execution, not the build identity.
# CTest expands these environment arguments when consuming the compiled artifact.
set(benchmark_tests [=[
add_test(cnet_owner_lifecycle_compare "$<TARGET_FILE:cnet_owner_lifecycle_compare>"
  "$ENV{CNET_COMPARE_BASELINE_DSO}" "$ENV{CNET_COMPARE_CANDIDATE_DSO}"
  "$ENV{CNET_COMPARE_REPORT}")
set_tests_properties(cnet_owner_lifecycle_compare PROPERTIES LABELS benchmark)
]=])
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(benchmark IN ITEMS cnet_tls_provider_parallel_benchmark cnet_tls_public_owner_benchmark)
    set(tls_test [=[
add_test(@benchmark@ "$<TARGET_FILE:@benchmark@>"
  "$ENV{CNET_TLS_MODE}" "$ENV{CNET_TLS_PAYLOAD}"
  "$ENV{CNET_TLS_OWNERS}" "$ENV{CNET_TLS_TEMPERATURE}")
set_tests_properties(@benchmark@ PROPERTIES LABELS benchmark)
]=])
    string(CONFIGURE "${tls_test}" tls_test @ONLY)
    string(APPEND benchmark_tests "${tls_test}")
  endforeach()
  string(APPEND benchmark_tests [=[
add_test(cnet_io_benchmark_trace strace -ff -ttt -T -s 128
  -e trace=%network,read,readv,write,writev,poll,ppoll,epoll_ctl,epoll_wait,epoll_pwait,epoll_pwait2,io_uring_setup,io_uring_register,io_uring_enter,futex
  -o "$ENV{SALTS_BENCH_TRACE_PREFIX}" "$<TARGET_FILE:cnet_io_benchmark>" --no-color)
set_tests_properties(cnet_io_benchmark_trace PROPERTIES LABELS "benchmark;trace")
]=])
endif()

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/benchmarks-$<CONFIG>.cmake"
  CONTENT "${benchmark_tests}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/benchmark-tests.cmake" CONTENT [=[
if("@CMAKE_CONFIGURATION_TYPES@" STREQUAL "")
  include("${CMAKE_CURRENT_LIST_DIR}/benchmarks-@CMAKE_BUILD_TYPE@.cmake")
else()
  include("${CMAKE_CURRENT_LIST_DIR}/benchmarks-${CTEST_CONFIGURATION_TYPE}.cmake")
endif()
]=] @ONLY)
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES
  "${CMAKE_CURRENT_BINARY_DIR}/benchmark-tests.cmake")
