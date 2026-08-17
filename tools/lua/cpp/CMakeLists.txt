add_library(cpp_lua_bind INTERFACE)
target_include_directories(cpp_lua_bind INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Lua QUIET)
if(NOT Lua_FOUND)
  find_package(Lua51 QUIET)
endif()

if(Lua_FOUND OR LUA_FOUND OR LUA51_FOUND)
  set(CPP_LUA_BIND_TEST_LIBS cpp_lua_bind TurboUtils::TinyTest)
  if(TARGET Lua::Lua)
    list(APPEND CPP_LUA_BIND_TEST_LIBS Lua::Lua)
  elseif(LUA_LIBRARIES)
    list(APPEND CPP_LUA_BIND_TEST_LIBS ${LUA_LIBRARIES})
  endif()

  cmake_add_test(cpp_lua_bind_test
    SOURCES cpp_lua_bind_test.cpp cpp_lua_bind.hpp
    LIBS ${CPP_LUA_BIND_TEST_LIBS}
    FOLDER "tools/lua/cpp")

  set_target_properties(cpp_lua_bind_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)

  if(NOT TARGET Lua::Lua AND LUA_INCLUDE_DIR)
    target_include_directories(cpp_lua_bind_test PRIVATE ${LUA_INCLUDE_DIR})
  endif()
endif()
