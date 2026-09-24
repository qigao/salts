cmake_add_test(cmeta_tinymock_interface_test
  SOURCES tinymock_cmeta_interface_test.c
  LIBS Salts::TinyMock
  FOLDER "tinymock/tests")

set_target_properties(cmeta_tinymock_interface_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cmeta_tinymock_cflow_clock_interface_test
  SOURCES tinymock_cflow_clock_interface_test.c
  LIBS Salts::TinyMock Salts::CFlow
  FOLDER "tinymock/tests")

set_target_properties(cmeta_tinymock_cflow_clock_interface_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cmeta_tinymock_function_auto_mock_test
  SOURCES
    tinymock_function_auto_mock_test.c
    tinymock_function_consumer.c
  LIBS Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
  FOLDER "tinymock/tests")

salts_tinymock_override_functions(
  cmeta_tinymock_function_auto_mock_test
  HEADERS tinymock_function_fixture.h)

set_target_properties(cmeta_tinymock_function_auto_mock_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cmeta_tinymock_history_test
  SOURCES tinymock_history_test.c
  LIBS Salts::TinyMock
  FOLDER "tinymock/tests")

set_target_properties(cmeta_tinymock_history_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cmeta_tinymock_actions_test
  SOURCES tinymock_actions_test.c
  LIBS Salts::TinyMock
  FOLDER "tinymock/tests")

set_target_properties(cmeta_tinymock_actions_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cmeta_tinymock_selective_override_test
  SOURCES
    tinymock_selective_test.c
    tinymock_selective_consumer.c
    tinymock_selective_real.c
  LIBS Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
  FOLDER "tinymock/tests")

salts_tinymock_override_functions(
  cmeta_tinymock_selective_override_test
  HEADERS
    tinymock_selective_fixture.h
    tinymock_selective_extra_fixture.h
  FUNCTIONS
    tinymock_selective_mocked
    tinymock_selective_void
    tinymock_selective_extra)

set_target_properties(cmeta_tinymock_selective_override_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)


cmake_add_test(cmeta_tinymock_return_test
  SOURCES tinymock_return_test.c
  LIBS Salts::TinyMock
  FOLDER "tinymock/tests")

set_target_properties(cmeta_tinymock_return_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cmeta_tinymock_object_pointer_no_generic_test
  SOURCES tinymock_object_pointer_no_generic_test.c
  LIBS Salts::TinyMock
  FOLDER "tinymock/tests")

set_target_properties(cmeta_tinymock_object_pointer_no_generic_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

function(tinymock_add_compile_failure_test target_name source expected_diagnostic)
  add_executable(${target_name} EXCLUDE_FROM_ALL ${source})
  target_link_libraries(${target_name} PRIVATE Salts::TinyMock)
  target_include_directories(${target_name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
  set_target_properties(${target_name} PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    FOLDER "tinymock/tests/compile_fail")
  add_test(
    NAME ${target_name}
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            --config $<CONFIG> --target ${target_name})
  set_tests_properties(${target_name} PROPERTIES
    PASS_REGULAR_EXPRESSION "${expected_diagnostic}"
    RUN_SERIAL TRUE)
endfunction()

tinymock_add_compile_failure_test(
  cmeta_tinymock_unspecified_pointer_compile_fail
  compile_fail/tinymock_unspecified_pointer.c
  "TinyMock auto-mock parameter tinymock_bad_unspecified.value has unspecified ABI carrier")

tinymock_add_compile_failure_test(
  cmeta_tinymock_opaque_param_compile_fail
  compile_fail/tinymock_opaque_param.c
  "TinyMock auto-mock parameter tinymock_bad_opaque_param.input uses an unsupported ABI carrier")

tinymock_add_compile_failure_test(
  cmeta_tinymock_typedef_void_compile_fail
  compile_fail/tinymock_typedef_void.c
  "TinyMock auto-mock return for tinymock_bad_typedef_void uses CMETA_ABI_VOID with a non-literal void spelling")

tinymock_add_compile_failure_test(
  cmeta_tinymock_variadic_selection_compile_fail
  compile_fail/tinymock_variadic_selection.c
  "tinymock_required_FunctionDecl_tinymock_bad_variadic")
salts_tinymock_override_functions(
  cmeta_tinymock_variadic_selection_compile_fail
  HEADERS compile_fail/tinymock_variadic_header.h
  FUNCTIONS tinymock_bad_variadic)

tinymock_add_compile_failure_test(
  cmeta_tinymock_array_selection_compile_fail
  compile_fail/tinymock_array_selection.c
  "tinymock_required_FunctionDecl_tinymock_bad_array")
salts_tinymock_override_functions(
  cmeta_tinymock_array_selection_compile_fail
  HEADERS compile_fail/tinymock_array_header.h
  FUNCTIONS tinymock_bad_array)

tinymock_add_compile_failure_test(
  cmeta_tinymock_static_inline_selection_compile_fail
  compile_fail/tinymock_static_inline_selection.c
  "tinymock_required_FunctionDecl_tinymock_bad_inline")
salts_tinymock_override_functions(
  cmeta_tinymock_static_inline_selection_compile_fail
  HEADERS compile_fail/tinymock_static_inline_header.h
  FUNCTIONS tinymock_bad_inline)
