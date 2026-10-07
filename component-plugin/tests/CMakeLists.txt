cmake_add_test(cmeta_native_component_plugin_abi_test
  SOURCES component_plugin_abi_test.c
  LIBS Salts::ComponentPluginABI Salts::TinyTest
  FOLDER "component-plugin/tests")
cmake_add_test(cmeta_native_component_plugin_abi_cpp_test
  SOURCES component_plugin_abi_cpp_test.cpp
  LIBS Salts::ComponentPluginABI Salts::TinyTest
  FOLDER "component-plugin/tests")

set_target_properties(cmeta_native_component_plugin_abi_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_target_properties(cmeta_native_component_plugin_abi_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

add_library(cmeta_component_plugin_provider_fixture SHARED
  fixture_component_provider.c)
target_link_libraries(
  cmeta_component_plugin_provider_fixture
  PRIVATE Salts::ComponentPluginABI Salts::PluginABI)
set_target_properties(cmeta_component_plugin_provider_fixture PROPERTIES
  FOLDER "component-plugin/tests/fixtures"
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

cmake_add_test(cmeta_native_component_plugin_dso_test
  SOURCES component_plugin_dso_test.c
  LIBS Salts::Plugin Salts::ComponentPluginABI Salts::TinyTest
  DEFS "COMPONENT_PROVIDER_PLUGIN_PATH=\"$<TARGET_FILE:cmeta_component_plugin_provider_fixture>\""
  FOLDER "component-plugin/tests")
add_dependencies(
  cmeta_native_component_plugin_dso_test
  cmeta_component_plugin_provider_fixture)
set_target_properties(cmeta_native_component_plugin_dso_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
