cmake_add_test(platform_clock_test
  SOURCES platform_clock_test.c
  LIBS TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "platform/tests")
set_target_properties(platform_clock_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(platform_header_cpp_test
  SOURCES platform_header_cpp_test.cpp
  LIBS TurboUtils::Platform
  FOLDER "platform/tests")
set_target_properties(platform_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
