set(TARGET_NAME turbo_cnet)

find_package(c-ares CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

get_target_property(cnet_cares_configurations c-ares::cares
                    IMPORTED_CONFIGURATIONS)
if(NOT cnet_cares_configurations)
  set(cnet_cares_configurations NOCONFIG)
endif()

set(cnet_cares_link_items)
foreach(cnet_cares_configuration IN LISTS cnet_cares_configurations)
  if(cnet_cares_configuration STREQUAL "NOCONFIG")
    set(cnet_cares_configuration_suffix "")
  else()
    set(cnet_cares_configuration_suffix "_${cnet_cares_configuration}")
  endif()

  if(WIN32)
    get_target_property(
      cnet_cares_link_file c-ares::cares
      "IMPORTED_IMPLIB${cnet_cares_configuration_suffix}")
  else()
    get_target_property(
      cnet_cares_link_file c-ares::cares
      "IMPORTED_LOCATION${cnet_cares_configuration_suffix}")
  endif()
  if(NOT cnet_cares_link_file
     OR cnet_cares_link_file MATCHES "-NOTFOUND$")
    message(
      FATAL_ERROR
        "c-ares does not provide a link file for ${cnet_cares_configuration}")
  endif()

  if(cnet_cares_configuration STREQUAL "NOCONFIG")
    list(APPEND cnet_cares_link_items "${cnet_cares_link_file}")
  else()
    list(
      APPEND
      cnet_cares_link_items
      "$<$<CONFIG:${cnet_cares_configuration}>:${cnet_cares_link_file}>")
  endif()
endforeach()

add_library(${TARGET_NAME} SHARED
  src/cnet_client.c
  src/cnet_command.c
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
  $<TARGET_OBJECTS:turbo_wsparser>)
cmake_config_target(${TARGET_NAME}
  ALIAS Rocida::CNet
  FOLDER "cnet"
  EXPORT_NAME CNet)
target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
set_target_properties(${TARGET_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tools/wsparser)
target_link_libraries(
  ${TARGET_NAME}
  PUBLIC Rocida::NativeIO Rocida::Concurrency
  PRIVATE Rocida::UriParser Rocida::Platform ${cnet_cares_link_items} OpenSSL::SSL OpenSSL::Crypto
          $<$<PLATFORM_ID:Windows>:crypt32>)

install(TARGETS ${TARGET_NAME} EXPORT RocidaTargets)
if(WIN32)
  install(FILES "$<TARGET_FILE:c-ares::cares>" TYPE BIN)
endif()
install(FILES "${c-ares_DIR}/copyright"
        DESTINATION share/licenses/Rocida/c-ares)
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
