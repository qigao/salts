find_package(Lua REQUIRED)

add_library(salts_lua_binding STATIC salts_lua_cmeta.c)
add_library(Salts::Lua ALIAS salts_lua_binding)

target_include_directories(salts_lua_binding
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${LUA_INCLUDE_DIR})

target_link_libraries(salts_lua_binding
  PUBLIC Salts::CMeta
  PRIVATE ${LUA_LIBRARIES})

target_compile_features(salts_lua_binding PUBLIC c_std_11)
set_target_properties(salts_lua_binding PROPERTIES FOLDER "bindings")

install(TARGETS salts_lua_binding EXPORT SaltsTargets)
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
