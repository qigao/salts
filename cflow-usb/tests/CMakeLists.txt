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

add_library(cflow_usb_test_impl STATIC
  ../src/usb.c
  fake/fake_libusb.c)
target_compile_features(cflow_usb_test_impl PRIVATE c_std_11)
target_include_directories(cflow_usb_test_impl PRIVATE
  fake
  ../include)
target_link_libraries(cflow_usb_test_impl PRIVATE TurboUtils::Platform)
set_target_properties(cflow_usb_test_impl PROPERTIES
  FOLDER "cflow-usb/tests")

cmake_add_test(cflow_usb_state_test
  SOURCES cflow_usb_state_test.c
  LIBS cflow_usb_test_impl TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "cflow-usb/tests")
target_include_directories(cflow_usb_state_test PRIVATE fake ../include)
set_target_properties(cflow_usb_state_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
