cmake_add_test(salts_quickjs_cmeta_test
  SOURCES quickjs_cmeta_test.c
  LIBS Salts::QuickJS Salts::CSTL Salts::TinyTest qjs
  FOLDER "bindings/quickjs/tests")

set_target_properties(salts_quickjs_cmeta_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
