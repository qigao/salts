# TinyMock reflected free-function Test Build helper.
#
# The canonical backend does not parse arbitrary C. It creates a tiny
# translation unit that overrides CMeta's natural FunctionDecl* spellings and
# re-includes the production reflected API header. The resulting functions keep
# the exact original C ABI and exist only in the test target.

function(tinymock_add_function_mocks target)
  set(options)
  set(oneValueArgs)
  set(multiValueArgs HEADERS)
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT TARGET ${target})
    message(FATAL_ERROR
      "tinymock_add_function_mocks: target '${target}' does not exist")
  endif()
  if(NOT ARG_HEADERS)
    message(FATAL_ERROR
      "tinymock_add_function_mocks: HEADERS is required for '${target}'")
  endif()
  if(NOT TARGET Salts::TinyMock)
    message(FATAL_ERROR
      "tinymock_add_function_mocks: Salts::TinyMock target is unavailable")
  endif()

  set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/tinymock-generated/${target}")
  file(MAKE_DIRECTORY "${generated_dir}")

  set(index 0)
  foreach(header IN LISTS ARG_HEADERS)
    if(IS_ABSOLUTE "${header}")
      set(header_abs "${header}")
    else()
      get_filename_component(
        header_abs "${header}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    if(NOT EXISTS "${header_abs}")
      message(FATAL_ERROR
        "tinymock_add_function_mocks: reflected header does not exist: ${header_abs}")
    endif()

    file(TO_CMAKE_PATH "${header_abs}" header_include)
    math(EXPR index "${index} + 1")
    set(generated
      "${generated_dir}/tinymock_functions_${index}.c")

    file(WRITE "${generated}"
"#define TINYMOCk_FUNCTION_DEFINITIONS 1\n"
"#include <tinymock_function.h>\n"
"#include \"${header_include}\"\n")

    set_source_files_properties("${generated}" PROPERTIES GENERATED TRUE)
    target_sources(${target} PRIVATE "${generated}")
  endforeach()

  target_link_libraries(${target} PRIVATE Salts::TinyMock)
endfunction()
