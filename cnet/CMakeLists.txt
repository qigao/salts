set(TARGET_NAME turbo_cnet_experimental)

find_package(c-ares CONFIG REQUIRED)

add_library(${TARGET_NAME} STATIC
  src/cnet_callback.c
  src/cnet_client.c
  src/cnet_command.c
  src/cnet_dispatcher.c
  src/cnet_event.c
  src/cnet_module.c
  src/cnet_owner.c
  src/cnet_resolver.c
  src/cnet_session.c
  src/cnet_shards.c
  src/cnet_transport.c
  src/cnet_transport_pipe.c
  src/cnet_uri.c)
cmake_config_target(${TARGET_NAME} FOLDER "cnet")
target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
target_link_libraries(
  ${TARGET_NAME}
  PUBLIC TurboUtils::NativeIO TurboUtils::Concurrency
  PRIVATE c-ares::cares)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
