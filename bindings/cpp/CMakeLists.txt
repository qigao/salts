add_library(salts_bindings_cpp INTERFACE)
add_library(Salts::BindingsCpp ALIAS salts_bindings_cpp)

set_target_properties(salts_bindings_cpp PROPERTIES
  EXPORT_NAME BindingsCpp)

target_include_directories(salts_bindings_cpp
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_link_libraries(salts_bindings_cpp INTERFACE Salts::CMeta)
target_compile_features(salts_bindings_cpp INTERFACE cxx_std_17)

install(TARGETS salts_bindings_cpp EXPORT SaltsTargets)
install(DIRECTORY include/ DESTINATION include)


if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
