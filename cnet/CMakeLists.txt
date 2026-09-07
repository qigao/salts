set(TARGET_NAME salts_cnet)

find_package(c-ares CONFIG REQUIRED)
find_package(kcp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

get_target_property(cnet_cares_configurations c-ares::cares
                    IMPORTED_CONFIGURATIONS)

set(CNET_SOURCE_FILES
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
  src/cnet_transport_vsock.c
  src/cnet_uri.c
  src/cnet_websocket.c
  $<TARGET_OBJECTS:salts_wsparser>)

function(cnet_configure_target target_name)
  target_compile_features(${target_name} PUBLIC c_std_11)
  # BoringSSL archives contain C++ objects even though CNet exposes a C API.
  set_target_properties(${target_name} PROPERTIES LINKER_LANGUAGE CXX)
  target_include_directories(
    ${target_name}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tools/wsparser)
  target_link_libraries(
    ${target_name}
    PUBLIC Salts::NativeIO Salts::Concurrency
    PRIVATE Salts::UriParser Salts::Platform Salts::CSTL Salts::Core monocypher salts_reed
            ${cnet_cares_link_items} kcp::kcp c-ares::cares
            OpenSSL::SSL OpenSSL::Crypto
            $<$<PLATFORM_ID:Windows>:crypt32>)
endfunction()

add_library(${TARGET_NAME} SHARED ${CNET_SOURCE_FILES})
cmake_config_target(${TARGET_NAME}
  ALIAS Salts::CNet
  FOLDER "cnet"
  EXPORT_NAME CNet)
set_target_properties(${TARGET_NAME} PROPERTIES
  WINDOWS_EXPORT_ALL_SYMBOLS ON)
cnet_configure_target(${TARGET_NAME})

if(BUILD_TESTS OR BUILD_BENCHMARKS)
  # Diagnostics use a private static copy so profiling and deterministic test
  # faults add neither exported symbols nor hot-path branches to installed CNet.
  add_library(salts_cnet_profile STATIC ${CNET_SOURCE_FILES})
  cnet_configure_target(salts_cnet_profile)
  target_compile_definitions(salts_cnet_profile PUBLIC CNET_INTERNAL_PROFILING=1)
  if(BUILD_TESTS)
    target_compile_definitions(salts_cnet_profile PUBLIC CNET_INTERNAL_TESTING=1)
  endif()
  set_target_properties(salts_cnet_profile PROPERTIES FOLDER "cnet/diagnostics")
endif()

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
