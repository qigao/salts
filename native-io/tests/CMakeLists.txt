cmake_add_test(native_io_test
  SOURCES native_io_test.c
  LIBS TurboUtils::NativeIO TurboUtils::TinyTest
  FOLDER "native-io/tests")
set_target_properties(native_io_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

if(WIN32)
  target_link_libraries(native_io_test PRIVATE ws2_32)
endif()

cmake_add_test(native_io_header_cpp_test
  SOURCES native_io_header_cpp_test.cpp
  LIBS TurboUtils::NativeIO
  FOLDER "native-io/tests")
set_target_properties(native_io_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
