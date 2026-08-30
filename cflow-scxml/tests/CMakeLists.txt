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

cmake_add_test(cflow_scxml_event_io_contract_test
  SOURCES cflow_scxml_event_io_contract_test.c
  LIBS TurboUtils::CFlowScxml TurboUtils::Core TurboUtils::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "cflow-scxml/tests")

set_target_properties(cflow_scxml_event_io_contract_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cflow_scxml_w3c_conformance_test
  SOURCES cflow_scxml_w3c_conformance_test.c
  LIBS TurboUtils::CFlowScxml TurboUtils::Core TurboUtils::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
  FOLDER "cflow-scxml/tests")

target_compile_definitions(cflow_scxml_w3c_conformance_test PRIVATE
  CFLOW_SCXML_W3C_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/w3c")

set_target_properties(cflow_scxml_w3c_conformance_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cflow_scxml_cmeta_expr_test
  SOURCES cflow_scxml_cmeta_expr_test.c
  LIBS TurboUtils::CFlowScxml TurboUtils::CMeta TurboUtils::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
  FOLDER "cflow-scxml/tests")

set_target_properties(cflow_scxml_cmeta_expr_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cflow_scxml_cmeta_test
  SOURCES cflow_scxml_cmeta_test.c
  LIBS TurboUtils::CFlowScxml TurboUtils::TinyTest
  FOLDER "cflow-scxml/tests")

set_target_properties(cflow_scxml_cmeta_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cflow_scxml_cmeta_sequence_test
  SOURCES cflow_scxml_cmeta_sequence_test.c
  LIBS TurboUtils::CFlowScxml TurboUtils::STL TurboUtils::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
  FOLDER "cflow-scxml/tests")

set_target_properties(cflow_scxml_cmeta_sequence_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cflow_scxml_cmeta_foreach_test
  SOURCES cflow_scxml_cmeta_foreach_test.c
  LIBS TurboUtils::CFlowScxml TurboUtils::STL TurboUtils::TinyTest
  FOLDER "cflow-scxml/tests")

set_target_properties(cflow_scxml_cmeta_foreach_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cflow_scxml_cmeta_managed_foreach_test
  SOURCES cflow_scxml_cmeta_managed_foreach_test.c
  LIBS TurboUtils::CFlowScxml TurboUtils::STL TurboUtils::TinyTest
  FOLDER "cflow-scxml/tests")

set_target_properties(cflow_scxml_cmeta_managed_foreach_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
