cmake_add_test(cflow_process_test
  SOURCES cflow_process_test.c
  LIBS TurboUtils::CFlowProcess TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "cflow-process/tests")
set_target_properties(cflow_process_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cflow_process_header_cpp_test
  SOURCES cflow_process_header_cpp_test.cpp
  LIBS TurboUtils::CFlowProcess TurboUtils::TinyTest
  FOLDER "cflow-process/tests")
set_target_properties(cflow_process_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
