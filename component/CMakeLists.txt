# Public Component provider ABI and authoring surface.
#
# This target carries headers + canonical CMeta semantics only. Provider DSOs
# may depend on it without pulling the host Configurator runtime.
add_library(salts_component_abi INTERFACE)
cmake_config_target(
  salts_component_abi
  ALIAS Salts::ComponentABI
  FOLDER "component"
  EXPORT_NAME ComponentABI)
target_compile_features(salts_component_abi INTERFACE c_std_11)
target_include_directories(
  salts_component_abi
  INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>)
target_link_libraries(salts_component_abi INTERFACE Salts::CMeta)

# Host-side bounded static Component Configurator runtime.
add_library(salts_component STATIC
  src/component.c)

cmake_config_target(
  salts_component
  VERSION "${PROJECT_VERSION}" SOVERSION "${PROJECT_VERSION_MAJOR}"
  ALIAS Salts::Component
  FOLDER "component"
  EXPORT_NAME Component)

target_compile_features(salts_component PRIVATE c_std_11)
target_link_libraries(salts_component PUBLIC salts_component_abi)

install(
  TARGETS salts_component_abi salts_component
  EXPORT SaltsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  DIRECTORY include/salts
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
