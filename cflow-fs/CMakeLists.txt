add_library(turbo_cflow_fs STATIC
  src/fs.c)
cmake_config_target(
  turbo_cflow_fs
  ALIAS TurboUtils::CFlowFS
  FOLDER "cflow-fs"
  EXPORT_NAME CFlowFS)

target_compile_features(turbo_cflow_fs PUBLIC c_std_11)
target_include_directories(turbo_cflow_fs PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(turbo_cflow_fs
  PUBLIC TurboUtils::Core
  PRIVATE TurboUtils::CFlow TurboUtils::Platform)

install(TARGETS turbo_cflow_fs EXPORT TurboUtilsTargets)
install(
  DIRECTORY include/cflow
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
