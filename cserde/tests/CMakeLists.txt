cmake_add_test(cserde_token_test
  SOURCES cserde_token_test.c
  LIBS Rocida::CSerde Rocida::TinyTest
  FOLDER "cserde/tests")

cmake_add_test(cserde_reader_test
  SOURCES cserde_reader_test.c
  LIBS Rocida::CSerde Rocida::TinyTest
  FOLDER "cserde/tests")

cmake_add_test(cserde_writer_test
  SOURCES cserde_writer_test.c
  LIBS Rocida::CSerde Rocida::TinyTest
  FOLDER "cserde/tests")

add_library(cserde_recording_support STATIC support/recording.c)
target_link_libraries(cserde_recording_support PUBLIC Rocida::CSerde)
target_include_directories(
  cserde_recording_support
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/support)
cmake_config_target(cserde_recording_support FOLDER "cserde/tests")

cmake_add_test(cserde_recording_test
  SOURCES cserde_recording_test.c
  LIBS cserde_recording_support Rocida::TinyTest
  FOLDER "cserde/tests")

cmake_add_test(cserde_header_cpp_test
  SOURCES cserde_header_cpp_test.cpp
  LIBS Rocida::CSerde Rocida::TinyTest
  FOLDER "cserde/tests")

set_target_properties(
  cserde_token_test
  cserde_reader_test
  cserde_writer_test
  cserde_recording_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

set_target_properties(cserde_recording_support PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

set_target_properties(cserde_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
