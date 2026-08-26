cmake_add_test(cflow_usb_test
  SOURCES cflow_usb_test.c
  LIBS TurboUtils::CFlowUSB TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "cflow-usb/tests")

set_target_properties(cflow_usb_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cflow_usb_header_cpp_test
  SOURCES cflow_usb_header_cpp_test.cpp
  LIBS TurboUtils::CFlowUSB TurboUtils::TinyTest
  FOLDER "cflow-usb/tests")

set_target_properties(cflow_usb_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
