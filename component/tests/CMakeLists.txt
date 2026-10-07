cmake_add_test(cmeta_native_component_test
  SOURCES component_test.c
  LIBS Salts::Component Salts::TinyTest
  FOLDER "component/tests")
cmake_add_test(cmeta_native_component_cpp_test
  SOURCES component_cpp_test.cpp
  LIBS Salts::Component Salts::TinyTest
  FOLDER "component/tests")

set_target_properties(cmeta_native_component_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_target_properties(cmeta_native_component_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

add_test(
  NAME cmeta_native_component_installed_test
  COMMAND "${CMAKE_COMMAND}"
    "-DSALTS_BINARY_DIR=${CMAKE_BINARY_DIR}"
    "-DCONSUMER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/package_config/component"
    "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/installed-component"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_installed_component.cmake")
set_tests_properties(cmeta_native_component_installed_test PROPERTIES
  TIMEOUT 180
  RUN_SERIAL TRUE)
