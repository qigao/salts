set(TARGET_NAME turbo_chttp)
set(CHTTP_LIBRARY_VERSION 2.0.0)
set(CHTTP_ABI_VERSION 2)

find_package(llhttp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(${TARGET_NAME} SHARED
  src/chttp_client.c
  src/chttp_file_sink.c
  src/chttp_file_transfer.c
  src/chttp_files.c
  src/chttp_h2_frame.c
  src/chttp_h2_hpack.c
  src/chttp_h2_proto.c
  src/chttp_h2_session.c
  src/chttp_h2_server.c
  src/chttp_requests.c
  src/chttp_request.c
  src/chttp_response.c
  src/chttp_server.c
  src/chttp_server_parser.c
  src/chttp_server_response.c
  src/chttp_server_route.c
  src/chttp_session.c
  src/chttp_tls.c)
target_sources(${TARGET_NAME} PRIVATE src/chttp_websocket_handshake.c)
target_sources(${TARGET_NAME} PRIVATE src/chttp_websocket_server.c)
target_sources(${TARGET_NAME} PRIVATE src/chttp_websocket_client.c)
target_sources(${TARGET_NAME} PRIVATE src/chttp_websocket_pool.c)
cmake_config_target(${TARGET_NAME}
  ALIAS Rocida::CHTTP
  FOLDER "chttp"
  EXPORT_NAME CHTTP
  VERSION ${CHTTP_LIBRARY_VERSION}
  SOVERSION ${CHTTP_ABI_VERSION})
if(WIN32)
  set_target_properties(${TARGET_NAME} PROPERTIES
    OUTPUT_NAME "turbo_chttp-${CHTTP_ABI_VERSION}")
endif()
target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
# BoringSSL archives contain C++ objects even though CHTTP exposes a C API.
set_target_properties(${TARGET_NAME} PROPERTIES
  WINDOWS_EXPORT_ALL_SYMBOLS ON
  LINKER_LANGUAGE CXX)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(
  ${TARGET_NAME}
  PUBLIC Rocida::CNet
  PRIVATE llhttp::llhttp_static Rocida::Platform Rocida::Core Rocida::CFlow Rocida::UriParser
          OpenSSL::Crypto)

install(TARGETS ${TARGET_NAME} EXPORT RocidaTargets)
install(FILES "${llhttp_DIR}/copyright"
        DESTINATION share/licenses/Rocida/llhttp)
install(
  DIRECTORY include/chttp
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
