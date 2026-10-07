cmake_minimum_required(VERSION 3.20)
project(component_installed_consumer LANGUAGES C)

find_package(Salts CONFIG REQUIRED)

if(NOT TARGET Salts::ComponentABI)
  message(FATAL_ERROR "installed package is missing Salts::ComponentABI")
endif()
if(NOT TARGET Salts::Component)
  message(FATAL_ERROR "installed package is missing Salts::Component")
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

enable_testing()
add_test(NAME component_installed_consumer
  COMMAND component_installed_consumer)
