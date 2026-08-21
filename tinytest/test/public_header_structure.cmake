if(NOT DEFINED TINYTEST_HEADER)
  message(FATAL_ERROR "TINYTEST_HEADER is required")
endif()

file(READ "${TINYTEST_HEADER}" tinytest_header)
string(REGEX MATCH
  "(^|\n)[ \t]*static[ \t]+(inline[ \t]+)?[^;\n{]*ttest_[A-Za-z0-9_]+__[ \t]*\\("
  private_static_function
  "${tinytest_header}")

if(private_static_function)
  string(STRIP "${private_static_function}" private_static_function)
  message(FATAL_ERROR
    "tinytest.h still defines a private static function: ${private_static_function}")
endif()
