# Public plugin publication contract used by plugin DSOs.
#
# This target carries headers + canonical CMeta semantics only. It deliberately
# does not carry loader/registry/lifecycle implementation.
add_library(salts_plugin_abi INTERFACE)
cmake_config_target(
  salts_plugin_abi
  ALIAS Salts::PluginABI
  FOLDER "plugin"
  EXPORT_NAME PluginABI)
target_compile_features(salts_plugin_abi INTERFACE c_std_11)
target_include_directories(salts_plugin_abi INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(salts_plugin_abi INTERFACE Salts::CMeta)

# Host-side loader/registry/lifecycle runtime.
add_library(salts_plugin STATIC
  src/plugin.c
  src/plugin_registry.c)

if(WIN32)
  target_sources(salts_plugin PRIVATE src/plugin_windows.c)
else()
  target_sources(salts_plugin PRIVATE src/plugin_posix.c)
endif()

cmake_config_target(
  salts_plugin
  ALIAS Salts::Plugin
  FOLDER "plugin"
  EXPORT_NAME Plugin)

target_compile_features(salts_plugin PRIVATE c_std_11)
target_link_libraries(salts_plugin
  PUBLIC salts_plugin_abi
  PRIVATE Salts::Core Salts::Platform ${CMAKE_DL_LIBS})

install(
  TARGETS salts_plugin_abi salts_plugin
  EXPORT SaltsTargets)
install(
  DIRECTORY include/salts
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
