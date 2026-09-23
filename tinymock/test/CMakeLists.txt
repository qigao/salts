cmake_add_test(cmeta_tinymock_interface_test
  SOURCES tinymock_cmeta_interface_test.c
  LIBS Salts::TinyMock
  FOLDER "tinymock/tests")

set_target_properties(cmeta_tinymock_interface_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cmeta_tinymock_function_test
  SOURCES tinymock_function_test.c
  LIBS Salts::TinyMock
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
  FOLDER "tinymock/tests")

tinymock_add_function_mocks(cmeta_tinymock_function_test
  HEADERS reflected_dependency.h)

set_target_properties(cmeta_tinymock_function_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
