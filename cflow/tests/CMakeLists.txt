set(CFLOW_TEST_SUPPORT
  cflow_test_ops.c
  cflow_test_ops.h)

cmake_add_test(cflow_graph_test
  SOURCES
    cflow_graph_test.c
    ${CFLOW_TEST_SUPPORT}
  LIBS TurboUtils::CFlow TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_pipeline_test
  SOURCES
    cflow_pipeline_test.c
    ${CFLOW_TEST_SUPPORT}
  LIBS TurboUtils::CFlow TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_runtime_test
  SOURCES
    cflow_runtime_test.c
    ${CFLOW_TEST_SUPPORT}
  LIBS TurboUtils::CFlow TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_header_cpp_test
  SOURCES cflow_header_cpp_test.cpp
  LIBS TurboUtils::CFlow TurboUtils::TinyTest
  FOLDER "cflow/tests")

set_target_properties(
  cflow_graph_test
  cflow_pipeline_test
  cflow_runtime_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

set_target_properties(cflow_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
