set(CNET_TEST_PRIVATE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
cmake_add_test(cnet_handoff_test SOURCES cnet_handoff_test.c
  LIBS Salts::CNetManager Salts::TinyTest FOLDER "cnet/tests")
cmake_add_test(cnet_manager_test SOURCES cnet_manager_test.c
  LIBS Salts::CNetManager Salts::TinyTest FOLDER "cnet/tests")
set(CNET_WSPARSER_INCLUDE ${CMAKE_SOURCE_DIR}/tools/wsparser)

cmake_add_test(cnet_ipc_test
  SOURCES cnet_ipc_test.c
  LIBS cnet Salts::Core Salts::TinyTest
  FOLDER "cnet/tests")

cmake_add_test(cnet_datagram_external_test
  SOURCES cnet_datagram_external_test.c
  LIBS cnet_test_profile Salts::Core Salts::TinyTest
  INCLUDES ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER "cnet/tests")

cmake_add_test(cnet_websocket_tagged_test
  SOURCES cnet_websocket_tagged_test.c
  LIBS cnet Salts::Core Salts::TinyTest
  FOLDER "cnet/tests")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cmake_add_test(cnet_ipc_io_uring_test
    SOURCES cnet_ipc_test.c
    LIBS cnet Salts::Core Salts::TinyTest
    DEFS CNET_TEST_IO_URING=1
    FOLDER "cnet/tests")
  cmake_add_test(cnet_datagram_external_io_uring_test
    SOURCES cnet_datagram_external_test.c
    LIBS cnet_test_profile Salts::Core Salts::TinyTest
    INCLUDES ${CNET_TEST_PRIVATE_INCLUDE}
    DEFS CNET_TEST_IO_URING=1
    FOLDER "cnet/tests")
endif()

cmake_add_test(
  cnet_websocket_parser_test
  SOURCES
  cnet_websocket_parser_test.c
  LIBS
  cnet
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_WSPARSER_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_websocket_parser_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_websocket_test
  SOURCES
  cnet_websocket_test.c
  LIBS
  cnet
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_WSPARSER_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_websocket_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_session_test
  SOURCES
  cnet_session_test.c
  LIBS
  cnet
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_session_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_write_queue_test
  SOURCES
  cnet_write_queue_test.c
  LIBS
  cnet
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_write_queue_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_command_test
  SOURCES
  cnet_command_test.c
  LIBS
  cnet
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_command_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_command_profile_test
  SOURCES
  cnet_command_profile_test.c
  LIBS
  cnet_test_profile
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_command_profile_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)
# The release benchmark workflow explicitly builds cnet_command_test; keep its
# diagnostic attribution and write-ownership contracts in the same build closure.
add_dependencies(cnet_command_test cnet_command_profile_test cnet_write_queue_test)

cmake_add_test(
  cnet_uri_test
  SOURCES
  cnet_uri_test.c
  LIBS
  cnet
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_uri_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_tls_test
  SOURCES
  cnet_tls_test.c
  LIBS
  cnet_test_profile
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_tls_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/fixtures/ip-test-root-cert.pem" CNET_TLS_TEST_IP_CA)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/fixtures/ip-test-server-cert.pem" CNET_TLS_TEST_IP_CERT)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/fixtures/ip-test-server-key.pem" CNET_TLS_TEST_IP_KEY)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/fixtures/p256-test-server-cert.pem" CNET_TLS_TEST_P256_CERT)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/fixtures/p256-test-server-key.pem" CNET_TLS_TEST_P256_KEY)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/fixtures/rfc5929-sha384-cert.der" CNET_TLS_TEST_SHA384_DER)
target_compile_definitions(cnet_tls_test PRIVATE
  "CNET_TLS_TEST_IP_CA=\"${CNET_TLS_TEST_IP_CA}\""
  "CNET_TLS_TEST_IP_CERT=\"${CNET_TLS_TEST_IP_CERT}\""
  "CNET_TLS_TEST_IP_KEY=\"${CNET_TLS_TEST_IP_KEY}\""
  "CNET_TLS_TEST_P256_CERT=\"${CNET_TLS_TEST_P256_CERT}\""
  "CNET_TLS_TEST_P256_KEY=\"${CNET_TLS_TEST_P256_KEY}\""
  "CNET_TLS_TEST_SHA384_DER=\"${CNET_TLS_TEST_SHA384_DER}\"")
unset(CNET_TLS_TEST_IP_CA)
unset(CNET_TLS_TEST_IP_CERT)
unset(CNET_TLS_TEST_IP_KEY)
unset(CNET_TLS_TEST_P256_CERT)
unset(CNET_TLS_TEST_P256_KEY)
unset(CNET_TLS_TEST_SHA384_DER)

find_package(GmSSL CONFIG REQUIRED)

add_executable(cnet_gmssl_contract_test cnet_gmssl_contract_test.c)
target_link_libraries(cnet_gmssl_contract_test PRIVATE GmSSL::GmSSL Salts::TinyTest)
set_target_properties(
  cnet_gmssl_contract_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF
             FOLDER "cnet/tests")
add_test(NAME cnet_gmssl_contract_test COMMAND cnet_gmssl_contract_test)

# The unified release workflow explicitly builds cnet_tls_test before running
# the cnet_* CTest contract set. Keep the provider probe in that build closure
# so a registered test can never become "Not Run" due to a missing executable.
add_dependencies(cnet_tls_test cnet_gmssl_contract_test)

cmake_add_test(
  cnet_transport_test
  SOURCES
  cnet_transport_test.c
  LIBS
  cnet
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_transport_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_owner_test
  SOURCES
  cnet_owner_test.c
  LIBS
  cnet
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_owner_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_owner_profile_test
  SOURCES
  cnet_owner_test.c
  LIBS
  cnet_test_profile
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_owner_profile_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_event_test
  SOURCES
  cnet_event_test.c
  LIBS
  cnet
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_event_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_resolver_test
  SOURCES
  cnet_resolver_test.c
  LIBS
  cnet
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_resolver_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_shards_test
  SOURCES
  cnet_shards_test.c
  LIBS
  cnet
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_shards_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)


cmake_add_test(
  cnet_shards_multi_owner_test
  SOURCES
  cnet_shards_multi_owner_test.c
  LIBS
  cnet_test_profile
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_shards_multi_owner_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)
add_dependencies(cnet_shards_test cnet_shards_multi_owner_test)

cmake_add_test(
  cnet_dispatcher_test
  SOURCES
  cnet_dispatcher_test.c
  LIBS
  cnet
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_dispatcher_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_tcp_endpoint_test
  SOURCES
  cnet_tcp_endpoint_test.c
  LIBS
  Salts::TinyTest cnet
  Salts::Core
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_tcp_endpoint_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_api_test
  SOURCES
  cnet_api_test.c
  cnet_send_buffer_api_test.c
  LIBS
  cnet
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_api_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_native_io_sharded_ownership_test
  SOURCES
  cnet_native_io_sharded_ownership_test.c
  LIBS
  cnet
  Salts::Core
  Salts::NativeIO
  Salts::TinyTest
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_native_io_sharded_ownership_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)
# The release workflow explicitly builds cnet_api_test. Keep upper-layer
# CNet/NativeIO conformance in the same build closure.
add_dependencies(cnet_api_test
  cnet_native_io_sharded_ownership_test
  cnet_tcp_endpoint_test
  cnet_external_progress_test
  cnet_external_listener_test
  cnet_external_tls_listener_test)

cmake_add_test(
  cnet_external_listener_test
  SOURCES
  cnet_external_listener_test.c
  LIBS
  Salts::TinyTest cnet
  Salts::Core
  Salts::NativeIO
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_external_listener_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_external_tls_listener_test
  SOURCES
  cnet_external_tls_listener_test.c
  LIBS
  Salts::TinyTest cnet
  Salts::Core
  Salts::NativeIO
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_external_tls_listener_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/fixtures/ip-test-root-cert.pem"
     CNET_EXTERNAL_TLS_CA)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/fixtures/ip-test-server-cert.pem"
     CNET_EXTERNAL_TLS_CERT)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_LIST_DIR}/fixtures/ip-test-server-key.pem"
     CNET_EXTERNAL_TLS_KEY)
target_compile_definitions(cnet_external_tls_listener_test PRIVATE
  "CNET_EXTERNAL_TLS_CA=\"${CNET_EXTERNAL_TLS_CA}\""
  "CNET_EXTERNAL_TLS_CERT=\"${CNET_EXTERNAL_TLS_CERT}\""
  "CNET_EXTERNAL_TLS_KEY=\"${CNET_EXTERNAL_TLS_KEY}\"")
unset(CNET_EXTERNAL_TLS_CA)
unset(CNET_EXTERNAL_TLS_CERT)
unset(CNET_EXTERNAL_TLS_KEY)

cmake_add_test(
  cnet_external_progress_test
  SOURCES
  cnet_external_progress_test.c
  LIBS
  Salts::TinyTest cnet
  Salts::Core
  Salts::NativeIO
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_external_progress_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_api_profile_test
  SOURCES
  cnet_api_test.c
  cnet_send_buffer_api_test.c
  LIBS
  cnet_test_profile
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_api_profile_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cmake_add_test(
    cnet_vsock_integration_test
    SOURCES
    cnet_vsock_integration_test.c
    LIBS
    Salts::TinyTest cnet
    FOLDER
    "cnet/tests")
  set_target_properties(
    cnet_vsock_integration_test
    PROPERTIES C_STANDARD 11
               C_STANDARD_REQUIRED ON
               C_EXTENSIONS OFF)
  set_tests_properties(cnet_vsock_integration_test PROPERTIES SKIP_RETURN_CODE
                                                              77)
endif()

find_program(CNET_POWERSHELL_EXECUTABLE NAMES pwsh powershell)
if(CNET_POWERSHELL_EXECUTABLE)
  add_test(
    NAME cnet_io_trace_test
    COMMAND ${CNET_POWERSHELL_EXECUTABLE}
            -NoProfile
            -File ${CMAKE_CURRENT_SOURCE_DIR}/cnet_io_trace_test.ps1)
  add_test(
    NAME cnet_scaling_verifier_test
    COMMAND ${CNET_POWERSHELL_EXECUTABLE}
            -NoProfile
            -File ${CMAKE_CURRENT_SOURCE_DIR}/cnet_scaling_verifier_test.ps1)
endif()

cmake_add_test(
  cnet_datagram_test
  SOURCES
  cnet_datagram_test.c
  LIBS
  cnet
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_datagram_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_kcp_test
  SOURCES
  cnet_kcp_test.c
  LIBS
  cnet
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_kcp_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_secure_kcp_test
  SOURCES
  cnet_secure_kcp_test.c
  LIBS
  cnet
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_secure_kcp_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_packet_endpoint_test
  SOURCES
  cnet_packet_endpoint_test.c
  LIBS
  cnet
  Salts::Core
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_packet_endpoint_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_stop_contract_test
  SOURCES
  cnet_stop_contract_test.c
  LIBS
  cnet_test_profile
  Salts::TinyTest
  INCLUDES
  ${CNET_TEST_PRIVATE_INCLUDE}
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_stop_contract_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_benchmark_stats_test
  SOURCES
  cnet_benchmark_stats_test.c
  ../benchmarks/cnet_benchmark_stats.c
  LIBS
  Salts::Platform
  Salts::TinyTest
  FOLDER
  "cnet/tests")
target_include_directories(cnet_benchmark_stats_test PRIVATE ../benchmarks)
set_target_properties(
  cnet_benchmark_stats_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_io_benchmark_config_test
  SOURCES
  cnet_io_benchmark_config_test.c
  ../benchmarks/cnet_io_benchmark_config.c
  LIBS
  Salts::NativeIO
  Salts::TinyTest
  FOLDER
  "cnet/tests")
target_include_directories(cnet_io_benchmark_config_test PRIVATE ../benchmarks)
set_target_properties(
  cnet_io_benchmark_config_test
  PROPERTIES C_STANDARD 11
             C_STANDARD_REQUIRED ON
             C_EXTENSIONS OFF)

cmake_add_test(
  cnet_header_cpp_test
  SOURCES
  cnet_header_cpp_test.cpp
  LIBS
  Salts::TinyTest cnet
  FOLDER
  "cnet/tests")
set_target_properties(
  cnet_header_cpp_test
  PROPERTIES CXX_STANDARD 17
             CXX_STANDARD_REQUIRED ON
             CXX_EXTENSIONS OFF)
