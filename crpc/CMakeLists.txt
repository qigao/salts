set(TARGET_NAME turbo_crpc)

add_library(${TARGET_NAME} STATIC
  src/crpc_common.c
  src/crpc_client.c
  src/crpc_json.c
  src/crpc_request_reply.c)
cmake_config_target(${TARGET_NAME}
  ALIAS Rocida::CRPC
  FOLDER "crpc"
  EXPORT_NAME CRPC)
target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(
  ${TARGET_NAME}
  PUBLIC Rocida::CHTTP Rocida::CMeta Rocida::CSerde
  PRIVATE Rocida::JsonCSerdeAdapter Rocida::Core)

install(TARGETS ${TARGET_NAME} EXPORT RocidaTargets)
install(
  DIRECTORY include/crpc
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
