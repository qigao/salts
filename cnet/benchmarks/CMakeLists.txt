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
  LIBS salts_cnet_profile Salts::TinyTest ${CNET_LIBUV_TARGET}
  FOLDER "cnet/benchmarks")
target_include_directories(cnet_io_benchmark PRIVATE ../src)
set_target_properties(cnet_io_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
  LINKER_LANGUAGE CXX)

# #288 matched performance runner. It deliberately does not link either CNet
# variant: each sample dlopens/LoadLibrary's exactly one uninstrumented B0/B1
# DSO so duplicate C symbols cannot interpose across variants.
cmake_add_benchmark(cnet_owner_lifecycle_benchmark
  SOURCES ../tests/cnet_owner_lifecycle_benchmark_test.c cnet_benchmark_stats.c
  LIBS Salts::Platform Salts::NativeIO Salts::Core ${CMAKE_DL_LIBS}
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "cnet/benchmarks")
add_dependencies(cnet_owner_lifecycle_benchmark
  salts_cnet
  salts_cnet_direct_experiment)
set_target_properties(cnet_owner_lifecycle_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

if(BUILD_TESTS)
  add_test(
    NAME cnet_owner_lifecycle_benchmark
    COMMAND $<TARGET_FILE:cnet_owner_lifecycle_benchmark>
            $<TARGET_FILE:salts_cnet>
            $<TARGET_FILE:salts_cnet_direct_experiment>
            ${CMAKE_SOURCE_DIR}/native-io-results/cnet-owner-lifecycle-benchmark.md)
endif()
