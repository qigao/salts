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
  SOURCES cnet_io_benchmark.c
  LIBS turbo_cnet_experimental TurboUtils::TinyTest ${CNET_LIBUV_TARGET}
  FOLDER "cnet/benchmarks")
set_target_properties(cnet_io_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
