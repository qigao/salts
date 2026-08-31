if(NOT (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
        ((APPLE OR CMAKE_SYSTEM_NAME MATCHES "^(FreeBSD|OpenBSD|NetBSD|DragonFly)$")
         AND CMAKE_SIZEOF_VOID_P EQUAL 8)))
  return()
endif()

find_package(libuv CONFIG REQUIRED)
if(TARGET libuv::uv_a)
  set(NATIVE_IO_LIBUV_TARGET libuv::uv_a)
elseif(TARGET libuv::uv)
  set(NATIVE_IO_LIBUV_TARGET libuv::uv)
else()
  message(FATAL_ERROR "libuv package did not provide libuv::uv_a or libuv::uv")
endif()

cmake_add_benchmark(native_io_benchmark
  SOURCES native_io_benchmark.c
  LIBS TurboUtils::NativeIO TurboUtils::TinyTest ${NATIVE_IO_LIBUV_TARGET}
  FOLDER "native-io/benchmarks")
set_target_properties(native_io_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_benchmark(native_io_pipe_benchmark
  SOURCES native_io_pipe_benchmark.c
  LIBS TurboUtils::NativeIO TurboUtils::TinyTest
  FOLDER "native-io/benchmarks")
set_target_properties(native_io_pipe_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
