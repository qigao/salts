cmake_add_executable(junit_to_html
  SOURCES junit_to_html.c
  LIBS Salts::Core Salts::CmdParser Salts::XmlParser
  FOLDER "tinytest/tools")

target_compile_features(junit_to_html PRIVATE c_std_11)

add_custom_command(
  TARGET junit_to_html POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/dashboard.html
          $<TARGET_FILE_DIR:junit_to_html>/dashboard.html
  COMMENT "Copying JUnit dashboard template"
  VERBATIM)
