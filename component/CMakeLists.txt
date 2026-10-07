add_library(salts_component STATIC
  src/component.c)

cmake_config_target(
  salts_component
  VERSION "${PROJECT_VERSION}" SOVERSION "${PROJECT_VERSION_MAJOR}"
  ALIAS Salts::Component
  FOLDER "component"
  EXPORT_NAME Component)

target_compile_features(salts_component PUBLIC c_std_11)
target_include_directories(
  salts_component
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(salts_component PUBLIC Salts::CMeta)

install(
  TARGETS salts_component
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
