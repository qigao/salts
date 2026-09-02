set(CNET_TEST_PRIVATE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(CNET_WSPARSER_INCLUDE ${CMAKE_SOURCE_DIR}/tools/wsparser)

cmake_add_test(cnet_websocket_parser_test
  SOURCES cnet_websocket_parser_test.c
  LIBS turbo_cnet TurboUtils::TinyTest
  INCLUDES ${CNET_WSPARSER_INCLUDE}
  FOLDER "cnet/tests")
set_target_properties(cnet_websocket_parser_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cnet_websocket_test
  SOURCES cnet_websocket_test.c
  LIBS turbo_cnet TurboUtils::TinyTest
  INCLUDES ${CNET_WSPARSER_INCLUDE}
  FOLDER "cnet/tests")
set_target_properties(cnet_websocket_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cnet_session_test
  SOURCES cnet_session_test.c
  LIBS turbo_cnet TurboUtils::TinyTest
  INCLUDES ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER "cnet/tests")
set_target_properties(cnet_session_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cnet_command_test
  SOURCES cnet_command_test.c
  LIBS turbo_cnet TurboUtils::TinyTest
  INCLUDES ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER "cnet/tests")
set_target_properties(cnet_command_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cnet_uri_test
  SOURCES cnet_uri_test.c
  LIBS turbo_cnet TurboUtils::TinyTest
  INCLUDES ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER "cnet/tests")
set_target_properties(cnet_uri_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cnet_tls_test
  SOURCES cnet_tls_test.c
  LIBS turbo_cnet TurboUtils::TinyTest OpenSSL::SSL
  INCLUDES ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER "cnet/tests")
set_target_properties(cnet_tls_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cnet_transport_test
  SOURCES cnet_transport_test.c
  LIBS turbo_cnet TurboUtils::TinyTest
  INCLUDES ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER "cnet/tests")
set_target_properties(cnet_transport_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cnet_owner_test
  SOURCES cnet_owner_test.c
  LIBS turbo_cnet TurboUtils::TinyTest
  INCLUDES ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER "cnet/tests")
set_target_properties(cnet_owner_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cnet_event_test
  SOURCES cnet_event_test.c
  LIBS turbo_cnet TurboUtils::TinyTest
  INCLUDES ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER "cnet/tests")
set_target_properties(cnet_event_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cnet_resolver_test
  SOURCES cnet_resolver_test.c
  LIBS turbo_cnet TurboUtils::TinyTest
  INCLUDES ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER "cnet/tests")
set_target_properties(cnet_resolver_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cnet_shards_test
  SOURCES cnet_shards_test.c
  LIBS turbo_cnet TurboUtils::TinyTest
  INCLUDES ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER "cnet/tests")
set_target_properties(cnet_shards_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cnet_dispatcher_test
  SOURCES cnet_dispatcher_test.c
  LIBS turbo_cnet TurboUtils::TinyTest
  INCLUDES ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER "cnet/tests")
set_target_properties(cnet_dispatcher_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cnet_api_test
  SOURCES cnet_api_test.c
  LIBS turbo_cnet TurboUtils::TinyTest
  INCLUDES ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER "cnet/tests")
set_target_properties(cnet_api_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cnet_benchmark_stats_test
  SOURCES cnet_benchmark_stats_test.c ../benchmarks/cnet_benchmark_stats.c
  LIBS TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "cnet/tests")
target_include_directories(cnet_benchmark_stats_test PRIVATE ../benchmarks)
set_target_properties(cnet_benchmark_stats_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cnet_header_cpp_test
  SOURCES cnet_header_cpp_test.cpp
  LIBS turbo_cnet
  FOLDER "cnet/tests")
set_target_properties(cnet_header_cpp_test PROPERTIES
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
