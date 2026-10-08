set(TARGET_NAME cmeta_cflow_cnet)

add_library(${TARGET_NAME}
  io_cnet_adapter.c
  cnet_domain_actor.c)

cmake_config_target(
  ${TARGET_NAME}
  VERSION "${PROJECT_VERSION}" SOVERSION "${PROJECT_VERSION_MAJOR}"
  ALIAS Salts::CFlowCNet
  FOLDER "cflow/cnet-adapter"
  EXPORT_NAME CFlowCNet)

set_target_properties(${TARGET_NAME} PROPERTIES
  WINDOWS_EXPORT_ALL_SYMBOLS ON)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/cflow/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(
  ${TARGET_NAME}
  PUBLIC Salts::CFlow Salts::CNet)

install(
  TARGETS ${TARGET_NAME}
  EXPORT SaltsTargets)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
