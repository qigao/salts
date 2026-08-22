# CMeta public API regression tests.

cmake_add_test(cmeta_core_test
  SOURCES cmeta_core_test.c cmeta_traits_peer.c
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_header_cpp_test
  SOURCES cmeta_header_cpp_test.cpp
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_collector_test
  SOURCES cmeta_collector_test.c
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

add_executable(cmeta_language_surface_test cmeta_language_surface_test.c)
target_link_libraries(cmeta_language_surface_test PRIVATE TurboUtils::CMeta)
set_target_properties(cmeta_language_surface_test PROPERTIES
  FOLDER "cmeta/tests"
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
add_test(NAME cmeta_language_surface COMMAND cmeta_language_surface_test)

set_target_properties(cmeta_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)

set_target_properties(cmeta_core_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

set_target_properties(cmeta_collector_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
