cmake_minimum_required(VERSION 3.20)
project(CFlowInstalledConsumer LANGUAGES C)

find_package(Salts 1.6 CONFIG REQUIRED)

add_executable(cflow_installed_consumer main.c)
target_link_libraries(cflow_installed_consumer PRIVATE Salts::CFlow)

set_target_properties(cflow_installed_consumer PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
