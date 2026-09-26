find_package(qjs CONFIG REQUIRED)

add_library(salts_quickjs_binding STATIC salts_quickjs_cmeta.c)
add_library(Salts::QuickJS ALIAS salts_quickjs_binding)

target_include_directories(salts_quickjs_binding
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_link_libraries(salts_quickjs_binding
  PUBLIC Salts::CMeta qjs)

target_compile_features(salts_quickjs_binding PUBLIC c_std_11)
set_target_properties(salts_quickjs_binding PROPERTIES FOLDER "bindings")

install(TARGETS salts_quickjs_binding EXPORT SaltsTargets)
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
