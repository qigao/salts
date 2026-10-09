cmake_minimum_required(VERSION 3.20)
project(component_installed_consumer LANGUAGES C)

if(NOT DEFINED SALTS_TEST_PREFIX OR NOT DEFINED SALTS_TEST_VERSION)
  message(FATAL_ERROR "the SDK prefix and expected version are required")
endif()

# Salts 2.3 intentionally introduces an incompatible native Component ABI.
# SameMinorVersion admission must reject a non-EXACT request for 2.2;
# checking only an older major would fail to catch accidental 2.2 admission.
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.[0-9]+$" sdk_semver "${SALTS_TEST_VERSION}")
if(NOT sdk_semver)
  message(FATAL_ERROR "expected an exact numeric Salts SDK version")
endif()
set(sdk_major "${CMAKE_MATCH_1}")
set(sdk_minor "${CMAKE_MATCH_2}")
if(sdk_minor LESS 1)
  message(FATAL_ERROR "this next-minor ABI admission fixture requires minor >= 1")
endif()
math(EXPR previous_minor "${sdk_minor} - 1")
set(previous_minor_version "${sdk_major}.${previous_minor}")
find_package(Salts ${previous_minor_version} CONFIG QUIET
  PATHS "${SALTS_TEST_PREFIX}" NO_DEFAULT_PATH)
if(Salts_FOUND)
  message(FATAL_ERROR
    "SDK ${SALTS_TEST_VERSION} accepted older ${previous_minor_version}.x request")
endif()
find_package(Salts ${SALTS_TEST_VERSION} EXACT CONFIG REQUIRED
  PATHS "${SALTS_TEST_PREFIX}" NO_DEFAULT_PATH)

if(NOT TARGET Salts::ComponentABI)
  message(FATAL_ERROR "installed package is missing Salts::ComponentABI")
endif()
if(NOT TARGET Salts::Component)
  message(FATAL_ERROR "installed package is missing Salts::Component")
endif()
if(NOT TARGET Salts::ComponentPlugin)
  message(FATAL_ERROR "installed package is missing Salts::ComponentPlugin")
endif()

get_target_property(component_abi_type Salts::ComponentABI TYPE)
if(NOT component_abi_type STREQUAL "INTERFACE_LIBRARY")
  message(FATAL_ERROR
    "Salts::ComponentABI must remain header/CMeta-only, got ${component_abi_type}")
endif()

get_target_property(component_abi_links
  Salts::ComponentABI INTERFACE_LINK_LIBRARIES)
if(component_abi_links MATCHES "Salts::Plugin")
  message(FATAL_ERROR "Salts::ComponentABI must not depend on Salts::Plugin")
endif()

get_target_property(component_links Salts::Component INTERFACE_LINK_LIBRARIES)
if(component_links MATCHES "Salts::Plugin")
  message(FATAL_ERROR "Salts::Component must not depend on Salts::Plugin")
endif()

add_executable(component_installed_consumer main.c)
target_link_libraries(component_installed_consumer PRIVATE Salts::Component)
target_compile_features(component_installed_consumer PRIVATE c_std_11)

# Rebuild and execute against the installed headers/libraries, not the
# in-tree source targets: catch layout/version skew in ComponentPlugin scopes.
add_executable(component_plugin_scope_installed_consumer scope.c)
target_link_libraries(component_plugin_scope_installed_consumer PRIVATE Salts::ComponentPlugin)
target_compile_features(component_plugin_scope_installed_consumer PRIVATE c_std_11)
set_target_properties(component_plugin_scope_installed_consumer PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

enable_testing()
add_test(NAME component_installed_consumer
  COMMAND component_installed_consumer)
add_test(NAME component_plugin_scope_installed_consumer
  COMMAND component_plugin_scope_installed_consumer)
