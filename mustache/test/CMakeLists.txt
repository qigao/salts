# Mustache Tests (current suite)
# Common configuration
set(TEST_LIBS TurboUtils::Mustache TurboUtils::JsonParser TurboUtils::TinyTest)
set(TEST_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_CURRENT_SOURCE_DIR}/../src)

cmake_add_test(test_mustache_json
  SOURCES test_json_integration.c
  LIBS ${TEST_LIBS}
  INCLUDES ${TEST_INCLUDES}
  FOLDER "mustache/tests")
cmake_add_test(test_mustache_spec
  SOURCES test_spec_adapted.c
  LIBS ${TEST_LIBS}
  INCLUDES ${TEST_INCLUDES}
  FOLDER "mustache/tests")
cmake_add_test(test_spec_runner
  SOURCES test_spec_runner.c
  LIBS ${TEST_LIBS}
  INCLUDES ${TEST_INCLUDES}
  FOLDER "mustache/tests")
cmake_add_test(test_scope
  SOURCES test_scope.c
  LIBS ${TEST_LIBS}
  INCLUDES ${TEST_INCLUDES}
  FOLDER "mustache/tests")
cmake_add_test(test_mustache_runtime
  SOURCES test_runtime_regressions.c
  LIBS ${TEST_LIBS}
  INCLUDES ${TEST_INCLUDES}
  FOLDER "mustache/tests")
cmake_add_test(test_mustache_xml
  SOURCES test_xml_integration.c
  LIBS ${TEST_LIBS} cxml
  INCLUDES ${TEST_INCLUDES}
  FOLDER "mustache/tests")

target_compile_definitions(test_spec_runner PRIVATE SPEC_JSON_DIR="${CMAKE_CURRENT_SOURCE_DIR}/spec")

# Test labels
set_tests_properties(test_mustache_json PROPERTIES LABELS "integration")
set_tests_properties(test_mustache_spec PROPERTIES LABELS "specification")
set_tests_properties(test_spec_runner PROPERTIES LABELS "specification;runner")
set_tests_properties(test_scope PROPERTIES LABELS "scope;unit")
set_tests_properties(test_mustache_runtime PROPERTIES LABELS "regression;unit")
set_tests_properties(test_mustache_xml PROPERTIES LABELS "integration;xml")
 
