if(NOT (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
        ((APPLE OR CMAKE_SYSTEM_NAME MATCHES "^(FreeBSD|OpenBSD|NetBSD|DragonFly)$")
         AND CMAKE_SIZEOF_VOID_P EQUAL 8)))
  return()
endif()

cmake_add_benchmark(native_io_pipe_benchmark
  SOURCES native_io_pipe_benchmark.c
  LIBS Salts::NativeIO Salts::TinyTest
  FOLDER "native-io/benchmarks")
set_target_properties(native_io_pipe_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)


cmake_add_benchmark(native_io_sharded_routing_benchmark
  SOURCES native_io_sharded_routing_benchmark.c
  LIBS Salts::NativeIO Salts::Platform
  FOLDER "native-io/benchmarks")
set_target_properties(native_io_sharded_routing_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)


cmake_add_benchmark(native_io_owner_handoff_benchmark
  SOURCES native_io_owner_handoff_benchmark.c
  LIBS Salts::NativeIO Salts::Platform
  FOLDER "native-io/benchmarks")
set_target_properties(native_io_owner_handoff_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)


cmake_add_benchmark(native_io_owner_mailbox_benchmark
  SOURCES native_io_owner_mailbox_benchmark.c
  LIBS Salts::NativeIO Salts::Platform
  FOLDER "native-io/benchmarks")
set_target_properties(native_io_owner_mailbox_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)


cmake_add_benchmark(native_io_owner_local_parallel_benchmark
  SOURCES native_io_owner_local_parallel_benchmark.c
  LIBS Salts::NativeIO Salts::Platform
  FOLDER "native-io/benchmarks")
set_target_properties(native_io_owner_local_parallel_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)


cmake_add_benchmark(native_io_pipe_styles_benchmark
  SOURCES native_io_pipe_styles_benchmark.c
  LIBS Salts::NativeIO Salts::Platform
  FOLDER "native-io/benchmarks")
set_target_properties(native_io_pipe_styles_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)


cmake_add_benchmark(native_io_stream_styles_benchmark
  SOURCES native_io_stream_styles_benchmark.c
  LIBS Salts::NativeIO Salts::Platform
  FOLDER "native-io/benchmarks")
set_target_properties(native_io_stream_styles_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
if(WIN32)
  target_link_libraries(native_io_stream_styles_benchmark PRIVATE ws2_32)
endif()


cmake_add_benchmark(native_io_udp_styles_benchmark
  SOURCES native_io_udp_styles_benchmark.c
  LIBS Salts::NativeIO Salts::Platform
  FOLDER "native-io/benchmarks")
set_target_properties(native_io_udp_styles_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
if(WIN32)
  target_link_libraries(native_io_udp_styles_benchmark PRIVATE ws2_32)
endif()


if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cmake_add_benchmark(native_io_vsock_styles_benchmark
    SOURCES native_io_vsock_styles_benchmark.c
    LIBS Salts::NativeIO Salts::Platform
    FOLDER "native-io/benchmarks")
  set_target_properties(native_io_vsock_styles_benchmark PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
endif()


if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cmake_add_benchmark(native_io_queue_depth_benchmark
    SOURCES native_io_queue_depth_benchmark.c
    LIBS Salts::NativeIO Salts::Platform
    FOLDER "native-io/benchmarks")
  set_target_properties(native_io_queue_depth_benchmark PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
endif()


if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cmake_add_benchmark(native_io_idle_observe_benchmark
    SOURCES native_io_idle_observe_benchmark.c
    LIBS Salts::NativeIO Salts::Platform
    FOLDER "native-io/benchmarks")
  set_target_properties(native_io_idle_observe_benchmark PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
endif()

foreach(benchmark IN ITEMS
    native_io_pipe_benchmark native_io_sharded_routing_benchmark
    native_io_owner_handoff_benchmark native_io_owner_mailbox_benchmark
    native_io_owner_local_parallel_benchmark native_io_pipe_styles_benchmark
    native_io_stream_styles_benchmark native_io_udp_styles_benchmark
    native_io_vsock_styles_benchmark native_io_queue_depth_benchmark
    native_io_idle_observe_benchmark)
  if(TARGET ${benchmark})
    add_test(NAME ${benchmark} COMMAND ${benchmark})
    set_tests_properties(${benchmark} PROPERTIES LABELS benchmark
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
  endif()
endforeach()
if(TARGET native_io_vsock_styles_benchmark)
  set_tests_properties(native_io_vsock_styles_benchmark PROPERTIES SKIP_RETURN_CODE 77)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # Trace only the benchmark process; tracing CTest itself duplicates measurement
  # markers in its captured-output writes and contaminates syscall attribution.
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/traces-$<CONFIG>.cmake" CONTENT [=[
add_test(native_io_stream_styles_benchmark_trace strace -ff -ttt -T -s 256
  -e trace=%network,read,readv,write,writev,poll,ppoll,epoll_ctl,epoll_wait,epoll_pwait,epoll_pwait2,io_uring_enter,futex
  -o "$ENV{SALTS_BENCH_TRACE_PREFIX}" "$<TARGET_FILE:native_io_stream_styles_benchmark>")
add_test(native_io_queue_depth_benchmark_trace strace -ff -ttt -T -s 128
  -e trace=io_uring_enter,poll,ppoll,write
  -o "$ENV{SALTS_BENCH_TRACE_PREFIX}" "$<TARGET_FILE:native_io_queue_depth_benchmark>")
set_tests_properties(native_io_stream_styles_benchmark_trace native_io_queue_depth_benchmark_trace
  PROPERTIES LABELS "benchmark;trace")
]=])
  file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/trace-tests.cmake" CONTENT [=[
if("@CMAKE_CONFIGURATION_TYPES@" STREQUAL "")
  include("${CMAKE_CURRENT_LIST_DIR}/traces-@CMAKE_BUILD_TYPE@.cmake")
else()
  include("${CMAKE_CURRENT_LIST_DIR}/traces-${CTEST_CONFIGURATION_TYPE}.cmake")
endif()
]=] @ONLY)
  set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES
    "${CMAKE_CURRENT_BINARY_DIR}/trace-tests.cmake")
endif()
