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

install(
  TARGETS salts_component_plugin_abi
  EXPORT SaltsTargets)

install(
  DIRECTORY include/salts
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
