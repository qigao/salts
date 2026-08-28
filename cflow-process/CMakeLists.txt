add_library(turbo_cflow_process STATIC src/process.c)
cmake_config_target(
  turbo_cflow_process
  ALIAS TurboUtils::CFlowProcess
  FOLDER "cflow-process"
  EXPORT_NAME CFlowProcess)
target_compile_features(turbo_cflow_process PUBLIC c_std_11)
target_include_directories(turbo_cflow_process PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(turbo_cflow_process
  PUBLIC TurboUtils::Core TurboUtils::CFlow
  PRIVATE TurboUtils::Platform)
install(TARGETS turbo_cflow_process EXPORT TurboUtilsTargets)
install(DIRECTORY include/cflow DESTINATION include FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
