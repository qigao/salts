set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(tinytest_include_dir "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(tinytest_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(tinytest_public_headers
  "${tinytest_include_dir}/tinytest.h"
  "${tinytest_include_dir}/tinytest.hpp"
  "${tinytest_include_dir}/tinymock.h"
  "${tinytest_include_dir}/tinymock.hpp")

set(tinytest_meta_headers
  "${tinytest_include_dir}/tinymeta/traits.h"
  "${tinytest_include_dir}/tinymeta/pp.h"
  "${tinytest_include_dir}/tinymeta/cmeta.h"
  "${tinytest_include_dir}/tinymeta/internal.h")

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

# Tests live in test/ and register executable targets and CTest entries.
add_subdirectory(test)

# JUnit reporting tools are owned alongside the framework that emits JUnit XML.
add_subdirectory(junit_to_html)
