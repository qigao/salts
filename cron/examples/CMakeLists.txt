add_executable(cron_example cron_example.c)

target_link_libraries(cron_example PRIVATE TurboUtils::Cron TurboUtils::Core)

cmake_config_target(cron_example FOLDER "cron/examples")
set_target_properties(cron_example PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
