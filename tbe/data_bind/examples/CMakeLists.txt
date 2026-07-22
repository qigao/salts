cmake_add_executable(data_bind_bmir_example
  SOURCES bmir_runtime.c
  LIBS TurboUtils::DataBind
  FOLDER "tbe/data_bind/examples")

target_compile_definitions(data_bind_bmir_example PRIVATE
  DATA_BIND_BMIR_EXAMPLE_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/packet.tbe")

set_target_properties(data_bind_bmir_example PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
