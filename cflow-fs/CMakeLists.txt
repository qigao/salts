add_library(turbo_cflow_fs STATIC
  src/fs.c
  src/fs_watch.c
  src/fs_watch_publisher.c)
if(WIN32)
  target_sources(turbo_cflow_fs PRIVATE src/fs_watch_windows.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(turbo_cflow_fs PRIVATE src/fs_watch_linux.c)
elseif(APPLE)
  target_sources(turbo_cflow_fs PRIVATE src/fs_watch_macos.c)
  target_link_libraries(turbo_cflow_fs PRIVATE
    "-framework CoreServices")
else()
  target_sources(turbo_cflow_fs PRIVATE src/fs_watch_unsupported.c)
endif()
cmake_config_target(
  turbo_cflow_fs
  ALIAS Rocida::CFlowFS
  FOLDER "cflow-fs"
  EXPORT_NAME CFlowFS)

target_compile_features(turbo_cflow_fs PUBLIC c_std_11)
target_include_directories(turbo_cflow_fs PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(turbo_cflow_fs
  PUBLIC Rocida::Core Rocida::CFlow
  PRIVATE Rocida::Platform)

install(TARGETS turbo_cflow_fs EXPORT RocidaTargets)
install(
  DIRECTORY include/cflow
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
