cmake_add_test(cnet_session_test
  SOURCES cnet_session_test.c
  LIBS turbo_cnet_experimental TurboUtils::TinyTest
  FOLDER "cnet/tests")
set_target_properties(cnet_session_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cnet_command_test
  SOURCES cnet_command_test.c
  LIBS turbo_cnet_experimental TurboUtils::TinyTest
  FOLDER "cnet/tests")
set_target_properties(cnet_command_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cnet_uri_test
  SOURCES cnet_uri_test.c
  LIBS turbo_cnet_experimental TurboUtils::TinyTest
  FOLDER "cnet/tests")
set_target_properties(cnet_uri_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cnet_transport_test
  SOURCES cnet_transport_test.c
  LIBS turbo_cnet_experimental TurboUtils::TinyTest
  FOLDER "cnet/tests")
set_target_properties(cnet_transport_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
