cmake_add_test(cmeta_tinymock_interface_test
  SOURCES tinymock_cmeta_interface_test.c
  LIBS Salts::TinyMock
  FOLDER "tinymock/tests")

set_target_properties(cmeta_tinymock_interface_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cmeta_tinymock_function_auto_mock_test
  SOURCES
    tinymock_function_auto_mock_test.c
    tinymock_function_consumer.c
  LIBS Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
  FOLDER "tinymock/tests")

salts_tinymock_override_functions(
  cmeta_tinymock_function_auto_mock_test
  HEADERS tinymock_function_fixture.h)

set_target_properties(cmeta_tinymock_function_auto_mock_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cmeta_tinymock_history_test
  SOURCES tinymock_history_test.c
  LIBS Salts::TinyMock
  FOLDER "tinymock/tests")

set_target_properties(cmeta_tinymock_history_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cmeta_tinymock_actions_test
  SOURCES tinymock_actions_test.c
  LIBS Salts::TinyMock
  FOLDER "tinymock/tests")

set_target_properties(cmeta_tinymock_actions_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cmeta_tinymock_selective_override_test
  SOURCES
    tinymock_selective_test.c
    tinymock_selective_consumer.c
    tinymock_selective_real.c
  LIBS Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
  FOLDER "tinymock/tests")

salts_tinymock_override_functions(
  cmeta_tinymock_selective_override_test
  HEADERS tinymock_selective_fixture.h
  FUNCTIONS
    tinymock_selective_mocked
    tinymock_selective_void)

set_target_properties(cmeta_tinymock_selective_override_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
