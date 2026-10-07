cmake_minimum_required(VERSION 3.20)
project(component_installed_consumer LANGUAGES C)

find_package(Salts CONFIG REQUIRED)

if(NOT TARGET Salts::Component)
  message(FATAL_ERROR "installed package is missing Salts::Component")
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
