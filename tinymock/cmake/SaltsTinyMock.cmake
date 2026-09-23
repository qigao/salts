include(CMakeParseArguments)

function(salts_tinymock_override_functions target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR
      "salts_tinymock_override_functions: unknown target '${target}'")
  endif()

  set(options)
  set(one_value_args)
  set(multi_value_args HEADERS FUNCTIONS)
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

  set(selection_preamble "")
  if(TINYMOCK_FUNCTIONS)
    string(APPEND selection_preamble
      "#define TINYMOCK_SELECTIVE_FUNCTION_OVERRIDES 1\n")
    foreach(function_name IN LISTS TINYMOCK_FUNCTIONS)
      if(NOT function_name MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR
          "salts_tinymock_override_functions: invalid function name "
          "'${function_name}'")
      endif()
      string(APPEND selection_preamble
        "#define TINYMOCK_SELECTED_FUNCTION_${function_name} "
        "TINYMOCk_PP_PROBE_()\n")
    endforeach()
  endif()

  if(TINYMOCK_FUNCTIONS)
    set(header_includes "")
    foreach(header IN LISTS TINYMOCK_HEADERS)
      string(APPEND header_includes "#include <${header}>\n")
    endforeach()

    set(selection_checks "")
    foreach(function_name IN LISTS TINYMOCK_FUNCTIONS)
      string(APPEND selection_checks
        "_Static_assert(sizeof(struct tinymock_required_FunctionDecl_${function_name}) == 1u, "
        "\"TinyMock selected function ${function_name} must be declared with FunctionDecl\");\n")
    endforeach()

    set(output
      "${CMAKE_CURRENT_BINARY_DIR}/${target}.tinymock.selected.c")
    file(GENERATE
      OUTPUT "${output}"
      CONTENT
"#define TINYTEST_NO_MAIN 1
#define TINYMOCK_GENERATE_FUNCTION_OVERRIDES 1
${selection_preamble}#include <tinymock_function.h>
${header_includes}
${selection_checks}")

    target_sources(${target} PRIVATE "${output}")
  else()
    set(index 0)
    foreach(header IN LISTS TINYMOCK_HEADERS)
      math(EXPR index "${index} + 1")
      set(output
        "${CMAKE_CURRENT_BINARY_DIR}/${target}.tinymock.${index}.c")

      file(GENERATE
        OUTPUT "${output}"
        CONTENT
"#define TINYTEST_NO_MAIN 1
#define TINYMOCK_GENERATE_FUNCTION_OVERRIDES 1
#include <tinymock_function.h>
#include <${header}>
")

      target_sources(${target} PRIVATE "${output}")
    endforeach()
  endif()

  target_link_libraries(${target} PRIVATE Salts::TinyMock)
endfunction()
