cmake_add_test(cflow_scxml_test
  SOURCES cflow_scxml_test.c
  LIBS TurboUtils::CFlowScxml TurboUtils::Core TurboUtils::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "cflow-scxml/tests")

target_compile_definitions(cflow_scxml_test PRIVATE
  CFLOW_SCXML_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

set_target_properties(cflow_scxml_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
