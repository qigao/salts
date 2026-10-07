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
