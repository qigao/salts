set(TARGET_NAME turbo_chttp)

find_package(llhttp CONFIG REQUIRED)

add_library(${TARGET_NAME} SHARED
  src/chttp_client.c
  src/chttp_h2_frame.c
  src/chttp_h2_hpack.c
  src/chttp_h2_proto.c
  src/chttp_requests.c
  src/chttp_request.c
  src/chttp_response.c
  src/chttp_server.c
  src/chttp_server_parser.c
  src/chttp_server_response.c
  src/chttp_server_route.c
  src/chttp_session.c
  src/chttp_tls.c)
cmake_config_target(${TARGET_NAME}
  ALIAS Rocida::CHTTP
  FOLDER "chttp"
  EXPORT_NAME CHTTP)
target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
set_target_properties(${TARGET_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(
  ${TARGET_NAME}
  PUBLIC Rocida::CNet
  PRIVATE llhttp::llhttp_static Rocida::Platform)

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
