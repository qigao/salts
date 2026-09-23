cmake_add_test(cmeta_tinymock_interface_test
  SOURCES tinymock_cmeta_interface_test.c
  LIBS Salts::TinyMock
  FOLDER "tinymock/tests")

set_target_properties(cmeta_tinymock_interface_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
