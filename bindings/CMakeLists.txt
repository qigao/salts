# Runtime language bindings consume canonical CMeta contracts only.
# Individual runtimes remain optional dependencies.
option(SALTS_BINDINGS_LUA "Build the Lua CMeta runtime binding" OFF)
option(SALTS_BINDINGS_QUICKJS "Build the QuickJS CMeta runtime binding" OFF)

if(SALTS_BINDINGS_LUA)
  add_subdirectory(lua)
endif()

if(SALTS_BINDINGS_QUICKJS)
  add_subdirectory(quickjs)
endif()


if(BUILD_TESTS AND SALTS_BINDINGS_LUA AND SALTS_BINDINGS_QUICKJS)
  add_subdirectory(tests)
endif()
