include(CMakeParseArguments)

function(salts_tinymock_override_functions target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR
      "salts_tinymock_override_functions: unknown target '${target}'")
  endif()

  set(options)
  set(one_value_args)
  set(multi_value_args HEADERS)
  cmake_parse_arguments(TINYMOCK
    "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  if(TINYMOCK_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "salts_tinymock_override_functions: unexpected arguments: "
      "${TINYMOCK_UNPARSED_ARGUMENTS}")
  endif()

  if(NOT TINYMOCK_HEADERS)
    message(FATAL_ERROR
      "salts_tinymock_override_functions requires HEADERS")
  endif()

  set(index 0)
  foreach(header IN LISTS TINYMOCK_HEADERS)
    math(EXPR index "${index} + 1")
    set(output
      "${CMAKE_CURRENT_BINARY_DIR}/${target}.tinymock.${index}.c")

    file(GENERATE
      OUTPUT "${output}"
      CONTENT
"#define TINYMOCK_GENERATE_FUNCTION_OVERRIDES 1
#include <tinymock_function.h>
#include <${header}>
")

    target_sources(${target} PRIVATE "${output}")
  endforeach()

  target_link_libraries(${target} PRIVATE Salts::TinyMock)
endfunction()
