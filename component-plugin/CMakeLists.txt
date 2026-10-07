# ABI-only publication contract for Component providers carried through
# ordinary Salts::Plugin Interface exports.
add_library(salts_component_plugin_abi INTERFACE)
cmake_config_target(
  salts_component_plugin_abi
  ALIAS Salts::ComponentPluginABI
  FOLDER "component-plugin"
  EXPORT_NAME ComponentPluginABI)

target_compile_features(salts_component_plugin_abi INTERFACE c_std_11)
target_include_directories(
  salts_component_plugin_abi
  INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>)
target_link_libraries(
  salts_component_plugin_abi
  INTERFACE Salts::ComponentABI Salts::PluginABI)

add_library(salts_component_plugin STATIC
  src/component_plugin.c)

cmake_config_target(
  salts_component_plugin
  VERSION "${PROJECT_VERSION}" SOVERSION "${PROJECT_VERSION_MAJOR}"
  ALIAS Salts::ComponentPlugin
  FOLDER "component-plugin"
  EXPORT_NAME ComponentPlugin)

target_compile_features(salts_component_plugin PRIVATE c_std_11)
target_link_libraries(
  salts_component_plugin
  PUBLIC salts_component_plugin_abi
  PRIVATE Salts::Component Salts::Plugin)

install(
  TARGETS salts_component_plugin_abi salts_component_plugin
  EXPORT SaltsTargets)

install(
  DIRECTORY include/salts
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
