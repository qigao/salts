if(WIN32)
  set(NATIVE_IO_BENCHMARK_SOURCE native_io_benchmark.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(NATIVE_IO_BENCHMARK_SOURCE native_io_benchmark_linux.c)
elseif((APPLE OR CMAKE_SYSTEM_NAME MATCHES "^(FreeBSD|OpenBSD|NetBSD|DragonFly)$")
       AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(NATIVE_IO_BENCHMARK_SOURCE native_io_benchmark_kqueue.c)
else()
  return()
endif()

cmake_add_benchmark(native_io_benchmark
  SOURCES ${NATIVE_IO_BENCHMARK_SOURCE}
  LIBS TurboUtils::NativeIO TurboUtils::TinyTest
  FOLDER "native-io/benchmarks")
if(WIN32)
  target_link_libraries(native_io_benchmark PRIVATE ws2_32)
endif()
set_target_properties(native_io_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

if(NOT WIN32)
  cmake_add_benchmark(native_io_pipe_benchmark
    SOURCES native_io_pipe_benchmark.c
    LIBS TurboUtils::NativeIO TurboUtils::TinyTest
    FOLDER "native-io/benchmarks")
  set_target_properties(native_io_pipe_benchmark PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
endif()
