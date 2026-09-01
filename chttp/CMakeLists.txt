set(TARGET_NAME turbo_chttp)

find_package(llhttp CONFIG REQUIRED)

add_library(${TARGET_NAME} STATIC
  src/chttp_client.c
  src/chttp_requests.c
  src/chttp_request.c
  src/chttp_response.c
  src/chttp_server.c
  src/chttp_server_parser.c
  src/chttp_server_response.c
  src/chttp_server_route.c
  src/chttp_session.c)
cmake_config_target(${TARGET_NAME}
  ALIAS TurboUtils::CHTTP
  FOLDER "chttp"
  EXPORT_NAME CHTTP)
target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(
  ${TARGET_NAME}
  PUBLIC TurboUtils::CNet
  PRIVATE llhttp::llhttp_static TurboUtils::Platform)

install(TARGETS ${TARGET_NAME} EXPORT TurboUtilsTargets)
install(
  DIRECTORY include/chttp
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
