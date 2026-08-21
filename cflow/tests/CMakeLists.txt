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

set_target_properties(
  cflow_graph_test
  cflow_pipeline_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
