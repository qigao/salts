cmake_add_test(salts_lua_invokable_test
  SOURCES lua_cmeta_invokable_test.c
  LIBS Salts::Lua Salts::TinyTest ${LUA_LIBRARIES}
  INCLUDES ${LUA_INCLUDE_DIR}
  FOLDER "bindings/lua/tests")

set_target_properties(salts_lua_invokable_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(salts_lua_conversion_test
  SOURCES lua_cmeta_conversion_test.c
  LIBS Salts::Lua Salts::TinyTest ${LUA_LIBRARIES}
  INCLUDES ${LUA_INCLUDE_DIR}
  FOLDER "bindings/lua/tests")

set_target_properties(salts_lua_conversion_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(salts_lua_object_test
  SOURCES lua_cmeta_object_test.c
  LIBS Salts::Lua Salts::TinyTest ${LUA_LIBRARIES}
  INCLUDES ${LUA_INCLUDE_DIR}
  FOLDER "bindings/lua/tests")

set_target_properties(salts_lua_object_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
