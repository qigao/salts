cmake_add_test(cbind_scalar_decode_test
  SOURCES cbind_scalar_decode_test.c
  LIBS TurboUtils::CBind cserde_recording_support TurboUtils::TinyTest
  FOLDER "cbind/tests")

cmake_add_test(cbind_struct_decode_test
  SOURCES cbind_struct_decode_test.c
  LIBS TurboUtils::CBind cserde_recording_support TurboUtils::TinyTest
  FOLDER "cbind/tests")

cmake_add_test(cbind_struct_runtime_test
  SOURCES cbind_struct_runtime_test.c
  LIBS TurboUtils::CBind cserde_recording_support TurboUtils::TinyTest
  FOLDER "cbind/tests")

cmake_add_test(cbind_transaction_test
  SOURCES cbind_transaction_test.c
  LIBS TurboUtils::CBind cserde_recording_support TurboUtils::TinyTest
  FOLDER "cbind/tests")

cmake_add_test(cbind_header_cpp_test
  SOURCES cbind_header_cpp_test.cpp
  LIBS TurboUtils::CBind TurboUtils::TinyTest
  FOLDER "cbind/tests")

set_target_properties(
  cbind_scalar_decode_test
  cbind_struct_decode_test
  cbind_struct_runtime_test
  cbind_transaction_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

set_target_properties(cbind_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
