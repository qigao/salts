add_library(c11_lua_bind INTERFACE)
target_include_directories(c11_lua_bind INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Lua QUIET)
if(NOT Lua_FOUND)
  find_package(Lua51 QUIET)
endif()

if(Lua_FOUND OR LUA_FOUND OR LUA51_FOUND)
  set(C11_LUA_BIND_TEST_LIBS c11_lua_bind TurboUtils::TinyTest)
  if(TARGET Lua::Lua)
    list(APPEND C11_LUA_BIND_TEST_LIBS Lua::Lua)
  elseif(LUA_LIBRARIES)
    list(APPEND C11_LUA_BIND_TEST_LIBS ${LUA_LIBRARIES})
  endif()

  cmake_add_test(c11_lua_bind_test
    SOURCES c11_lua_bind_test.c
    LIBS ${C11_LUA_BIND_TEST_LIBS}
    FOLDER "tools/lua")

  set_target_properties(c11_lua_bind_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON)

  if(NOT TARGET Lua::Lua AND LUA_INCLUDE_DIR)
    target_include_directories(c11_lua_bind_test PRIVATE ${LUA_INCLUDE_DIR})
  endif()
endif()

add_subdirectory(cpp)
