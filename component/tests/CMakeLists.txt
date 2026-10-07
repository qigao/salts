cmake_add_test(salts_component_test
  SOURCES component_test.c
  LIBS Salts::Component Salts::TinyTest
  FOLDER "component/tests")
cmake_add_test(salts_component_cpp_test
  SOURCES component_cpp_test.cpp
  LIBS Salts::Component Salts::TinyTest
  FOLDER "component/tests")

set_target_properties(salts_component_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_target_properties(salts_component_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
