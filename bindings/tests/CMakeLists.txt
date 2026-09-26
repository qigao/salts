cmake_add_test(salts_object_cross_runtime_test
  SOURCES object_cross_runtime_test.c
  LIBS Salts::Lua Salts::QuickJS Salts::TinyTest qjs ${LUA_LIBRARIES}
  INCLUDES ${LUA_INCLUDE_DIR}
  FOLDER "bindings/tests")

set_target_properties(salts_object_cross_runtime_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
