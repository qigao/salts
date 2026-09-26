cmake_add_test(salts_object_cross_runtime_test
  SOURCES object_cross_runtime_test.c
  LIBS Salts::Lua Salts::QuickJS Salts::TinyTest ${LUA_LIBRARIES}
  INCLUDES ${LUA_INCLUDE_DIR}
  FOLDER "bindings/tests")

set_target_properties(salts_object_cross_runtime_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)


cmake_add_test(salts_cpp_object_facade_test
  SOURCES cpp_object_facade_test.cpp
  LIBS Salts::BindingsCpp Salts::Lua Salts::QuickJS Salts::TinyTest
  FOLDER "bindings/tests")

set_target_properties(salts_cpp_object_facade_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
