cmake_minimum_required(VERSION 3.20)
project(TinyMockInstalledConsumer LANGUAGES C)

find_package(Salts CONFIG REQUIRED)

add_executable(tinymock_installed_consumer
  main.c
  consumer.c)

target_include_directories(tinymock_installed_consumer PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(tinymock_installed_consumer PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

salts_tinymock_override_functions(
  tinymock_installed_consumer
  HEADERS api.h
  FUNCTIONS
    tinymock_installed_add
    tinymock_installed_shutdown
    tinymock_installed_box_copy
    tinymock_installed_apply_callback
    tinymock_installed_callback_answer
    tinymock_installed_mode_echo)
