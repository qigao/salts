cmake_minimum_required(VERSION 3.25)
project(CMetaInstalledReflectionTests LANGUAGES C CXX)

if(NOT DEFINED ENV{CMETA_PACKAGE_ROOT} OR "$ENV{CMETA_PACKAGE_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CMETA_PACKAGE_ROOT must identify the installed Salts package")
endif()
file(TO_CMAKE_PATH "$ENV{CMETA_PACKAGE_ROOT}" cmeta_package_root)
unset(Salts_DIR CACHE)
unset(Salts_DIR)
find_package(Salts CONFIG REQUIRED PATHS "${cmeta_package_root}" NO_DEFAULT_PATH)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CmakeUtils.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/Sanitizers.cmake")
enable_testing()

# Reuse the declaration regressions against imported SDK targets. No source-tree
# public include directories are added to these consumers.
cmake_add_test(cmeta_installed_pp_test
  SOURCES ../cmeta_pp_test.c
  LIBS Salts::CMeta Salts::TinyTest FOLDER "cmeta/tests/installed")
cmake_add_test(cmeta_installed_pp_cpp_test
  SOURCES ../cmeta_pp_cpp_test.cpp
  LIBS Salts::CMeta Salts::TinyTest FOLDER "cmeta/tests/installed")
cmake_add_test(plugin_installed_declaration_test
  SOURCES ../../../plugin/tests/plugin_declaration_test.c
  LIBS Salts::Plugin Salts::TinyTest FOLDER "plugin/tests/installed")
cmake_add_test(plugin_installed_declaration_cpp_test
  SOURCES ../../../plugin/tests/plugin_declaration_cpp_test.cpp
  LIBS Salts::Plugin Salts::TinyTest FOLDER "plugin/tests/installed")
set_target_properties(cmeta_installed_pp_test plugin_installed_declaration_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_target_properties(cmeta_installed_pp_cpp_test plugin_installed_declaration_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

cmake_add_test(cmeta_installed_reflection_test
  SOURCES cmeta_installed_reflection_test.c
  LIBS Salts::CMeta Salts::TinyTest FOLDER "cmeta/tests/installed")
cmake_add_test(cmeta_installed_reflection_cpp_test
  SOURCES cmeta_installed_reflection_cpp_test.cpp
  LIBS Salts::CMeta Salts::TinyTest FOLDER "cmeta/tests/installed")
set_target_properties(cmeta_installed_reflection_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_target_properties(cmeta_installed_reflection_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
