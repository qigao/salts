cmake_add_test(chttp_request_test
  SOURCES chttp_request_test.c
  LIBS turbo_chttp llhttp::llhttp_static Rocida::TinyTest
  INCLUDES ../src
  FOLDER "chttp/tests")
set_target_properties(chttp_request_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(chttp_response_test
  SOURCES chttp_response_test.c
  LIBS turbo_chttp llhttp::llhttp_static Rocida::TinyTest
  INCLUDES ../src
  FOLDER "chttp/tests")
set_target_properties(chttp_response_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(chttp_server_parser_test
  SOURCES chttp_server_parser_test.c
  LIBS turbo_chttp llhttp::llhttp_static Rocida::TinyTest
  INCLUDES ../src
  FOLDER "chttp/tests")
set_target_properties(chttp_server_parser_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(chttp_server_test
  SOURCES chttp_server_test.c
  LIBS turbo_chttp Rocida::TinyTest
  FOLDER "chttp/tests")
set_target_properties(chttp_server_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(chttp_api_test
  SOURCES chttp_api_test.c
  LIBS turbo_chttp Rocida::TinyTest
  FOLDER "chttp/tests")
set_target_properties(chttp_api_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(chttp_requests_test
  SOURCES chttp_requests_test.c
  LIBS turbo_chttp Rocida::TinyTest
  FOLDER "chttp/tests")
set_target_properties(chttp_requests_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(chttp_header_cpp_test
  SOURCES chttp_header_cpp_test.cpp
  LIBS turbo_chttp
  FOLDER "chttp/tests")
set_target_properties(chttp_header_cpp_test PROPERTIES
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
