set(TARGET_NAME salts_cnet)

find_package(c-ares CONFIG REQUIRED)
find_package(kcp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

get_target_property(cnet_cares_configurations c-ares::cares
                    IMPORTED_CONFIGURATIONS)

add_library(${TARGET_NAME} SHARED
  src/cnet_client.c
  src/cnet_command.c
  src/cnet_datagram.c
  src/cnet_kcp.c
  src/cnet_kcp_fec.c
  src/cnet_kcp_secure.c
  src/cnet_secure_kcp.c
  src/cnet_packet_endpoint.c
  src/cnet_dispatcher.c
  src/cnet_event.c
  src/cnet_listener.c
  src/cnet_module.c
  src/cnet_owner.c
  src/cnet_resolver.c
  src/cnet_session.c
  src/cnet_shards.c
  src/cnet_tls.c
  src/cnet_transport.c
  src/cnet_transport_pipe.c
  src/cnet_uri.c
  src/cnet_websocket.c
  $<TARGET_OBJECTS:salts_wsparser>)
cmake_config_target(${TARGET_NAME}
  ALIAS Salts::CNet
  FOLDER "cnet"
  EXPORT_NAME CNet)
target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
# BoringSSL archives contain C++ objects even though CNet exposes a C API.
set_target_properties(${TARGET_NAME} PROPERTIES
  WINDOWS_EXPORT_ALL_SYMBOLS ON
  LINKER_LANGUAGE CXX)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tools/wsparser)
target_link_libraries(
  ${TARGET_NAME}
  PUBLIC Salts::NativeIO Salts::Concurrency
  PRIVATE Salts::UriParser Salts::Platform Salts::CSTL Salts::Core monocypher salts_reed
          ${cnet_cares_link_items} kcp::kcp c-ares::cares
          OpenSSL::SSL OpenSSL::Crypto
          $<$<PLATFORM_ID:Windows>:crypt32>)

install(TARGETS ${TARGET_NAME} EXPORT SaltsTargets)
if(WIN32)
  install(FILES "$<TARGET_FILE:c-ares::cares>" TYPE BIN)
endif()
install(FILES "${c-ares_DIR}/copyright"
        DESTINATION share/licenses/Salts/c-ares)
install(FILES "${kcp_DIR}/copyright"
        DESTINATION share/licenses/Salts/kcp)
install(FILES "${CMAKE_SOURCE_DIR}/vendor/reed/LICENSE"
        DESTINATION share/licenses/Salts/reed)
install(
  DIRECTORY include/cnet
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
