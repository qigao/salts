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

if(TARGET Salts::CMetaNative)
  cmake_add_test(cmeta_installed_native_test SOURCES ../cmeta_native_thunk_test.c ../cmeta_native_targets.c
    LIBS Salts::CMetaNative Salts::TinyTest FOLDER "cmeta/tests/installed")
  cmake_add_test(cmeta_installed_native_cpp_test SOURCES ../cmeta_native_thunk_cpp_test.cpp
    LIBS Salts::CMetaNative Salts::Platform Salts::TinyTest FOLDER "cmeta/tests/installed")
  cmake_add_test(cmeta_installed_native_object_test SOURCES ../cmeta_native_object_test.c
    ../cmeta_native_object_fixture.c
    LIBS Salts::CMetaNative Salts::TinyTest FOLDER "cmeta/tests/installed")
  cmake_add_test(cmeta_installed_native_static_call_test SOURCES ../cmeta_native_static_call_test.c
    ../cmeta_native_targets.c
    LIBS Salts::CMetaNative Salts::Platform Salts::TinyTest FOLDER "cmeta/tests/installed")
  set_target_properties(cmeta_installed_native_test cmeta_installed_native_object_test
    cmeta_installed_native_static_call_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  set_target_properties(cmeta_installed_native_cpp_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
endif()

cmake_add_test(cstl_installed_scope_test
  SOURCES ../../../cstl/tests/cstl_header_typed_test.c
  LIBS Salts::CSTL Salts::TinyTest FOLDER "cmeta/tests/installed")
set_target_properties(cstl_installed_scope_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

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

cmake_add_test(tinytest_installed_equality_test
  SOURCES ../../../tinytest/test/equality_test.c
  LIBS Salts::TinyTest FOLDER "tinytest/tests/installed")
cmake_add_test(tinytest_installed_cpp_test
  SOURCES ../../../tinytest/test/regression_cpp.cpp
  LIBS Salts::TinyTest FOLDER "tinytest/tests/installed")
set_target_properties(tinytest_installed_equality_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_target_properties(tinytest_installed_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
