cmake_add_test(
  test_cflow_graph
  SOURCES test_cflow_graph.c
  FOLDER "cflow/tests"
  LIBS TurboUtils::TinyTest TurboUtils::CFlow)
