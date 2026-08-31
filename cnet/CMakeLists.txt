set(TARGET_NAME turbo_cnet_experimental)

add_library(${TARGET_NAME} STATIC
  src/cnet_command.c
  src/cnet_session.c
  src/cnet_transport.c
  src/cnet_uri.c)
cmake_config_target(${TARGET_NAME} FOLDER "cnet")
target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(
  ${TARGET_NAME}
  PUBLIC TurboUtils::NativeIO TurboUtils::Concurrency)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
