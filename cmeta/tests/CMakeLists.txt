# CMeta public API regression tests.

cmake_add_test(cmeta_core_test
  SOURCES cmeta_core_test.c
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_header_cpp_test
  SOURCES cmeta_header_cpp_test.cpp
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

set_target_properties(cmeta_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)

set_target_properties(cmeta_core_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
