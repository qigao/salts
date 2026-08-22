set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(tinytest_include_dir "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(tinytest_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(tinytest_public_headers
  "${tinytest_include_dir}/tinytest.h"
  "${tinytest_include_dir}/tinytest.hpp"
  "${tinytest_include_dir}/tinymock.h"
  "${tinytest_include_dir}/tinymock.hpp"
  "${tinytest_include_dir}/traits.h")

set(tinytest_meta_headers
  "${tinytest_include_dir}/tinymeta/pp.h"
  "${tinytest_include_dir}/tinymeta/tinytest_cmeta.h"
  "${tinytest_include_dir}/tinymeta/tinytest_internal.h")

set(tinytest_sources
  "${tinytest_source_dir}/tinytest.c"
  "${tinytest_source_dir}/tinymock.c")

add_library(tinytest STATIC)
target_sources(tinytest PRIVATE
  ${tinytest_sources}
  ${tinytest_public_headers}
  ${tinytest_meta_headers})

source_group(TREE "${tinytest_source_dir}" PREFIX "Source Files"
  FILES ${tinytest_sources})
source_group(TREE "${tinytest_include_dir}" PREFIX "Header Files"
  FILES ${tinytest_public_headers} ${tinytest_meta_headers})

cmake_config_target(tinytest
  ALIAS TurboUtils::TinyTest
  FOLDER "tinytest"
  EXPORT_NAME TinyTest)

target_include_directories(tinytest
  PUBLIC
    $<BUILD_INTERFACE:${tinytest_include_dir}>
    $<INSTALL_INTERFACE:include>)

install(TARGETS tinytest
  EXPORT TurboUtilsTargets
  COMPONENT TinyTest)
install(FILES ${tinytest_public_headers}
  DESTINATION include
  COMPONENT TinyTest)
install(FILES ${tinytest_meta_headers}
  DESTINATION include/tinymeta
  COMPONENT TinyTest)

# Examples live in examples/ and are plain executables rather than CTest cases.
add_subdirectory(examples)

# TinyTest's internal probes are independent from downstream suites that merely
# link TurboUtils::TinyTest. Keep the historical default while allowing focused
# component CI to skip TinyTest's own self-test matrix.
option(TINYTEST_BUILD_SELF_TESTS "Build TinyTest self-tests and compile probes" ON)
if(TINYTEST_BUILD_SELF_TESTS)
  add_subdirectory(test)
endif()
