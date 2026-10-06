cmake_add_test(cmeta_trace_test
  SOURCES cmeta_trace_test.c
  LIBS Salts::CMeta Salts::TinyTest Salts::Platform
  FOLDER "cmeta/tests")
cmake_add_test(cmeta_trace_cpp_test
  SOURCES cmeta_trace_cpp_test.cpp cmeta_trace_peer.c
  LIBS Salts::CMeta Salts::TinyTest Salts::Platform
  FOLDER "cmeta/tests")
set_target_properties(cmeta_trace_test cmeta_trace_cpp_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_target_properties(cmeta_trace_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
set_tests_properties(cmeta_trace_test PROPERTIES TIMEOUT 60)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cmeta_trace_cpp_test PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-Werror=old-style-cast>")
endif()

cmake_add_test(cmeta_fastpath_test
  SOURCES cmeta_fastpath_test.c cmeta_fastpath_peer.c
  LIBS Salts::CMeta Salts::TinyTest Salts::Platform
  FOLDER "cmeta/tests")
cmake_add_test(cmeta_fastpath_cpp_test
  SOURCES cmeta_fastpath_cpp_test.cpp cmeta_fastpath_peer.c
  LIBS Salts::CMeta Salts::TinyTest Salts::Platform
  FOLDER "cmeta/tests")
cmake_add_test(cmeta_fastpath_coroutine_test
  SOURCES cmeta_fastpath_coroutine_test.c cmeta_fastpath_peer.c
  LIBS Salts::CMeta Salts::TinyTest Salts::Coroutine Salts::Platform
  FOLDER "cmeta/tests")
set_target_properties(cmeta_fastpath_test cmeta_fastpath_cpp_test cmeta_fastpath_coroutine_test
  PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_target_properties(cmeta_fastpath_cpp_test
  PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cmeta_fastpath_cpp_test PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-Werror=old-style-cast>")
endif()

function(cmeta_add_compile_failure_test target_name source expected_diagnostic)
  add_executable(${target_name} EXCLUDE_FROM_ALL ${source})
  target_link_libraries(${target_name} PRIVATE Salts::CMeta)
  set_target_properties(${target_name} PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    FOLDER "cmeta/tests/compile_fail")
  add_test(
    NAME ${target_name}
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            --config $<CONFIG> --target ${target_name})
  set_tests_properties(${target_name} PROPERTIES
    PASS_REGULAR_EXPRESSION "${expected_diagnostic}"
    RUN_SERIAL TRUE)
endfunction()

if(TARGET Salts::CMetaNative)
  cmeta_add_compile_failure_test(cmeta_native_static_shape_compile_fail
    compile_fail/cmeta_native_static_shape.c "CMeta native static call requires exact int")
  target_link_libraries(cmeta_native_static_shape_compile_fail PRIVATE Salts::CMetaNative Salts::Platform)
endif()

cmeta_add_compile_failure_test(
  cmeta_trace_wrong_backend_compile_fail
  compile_fail/cmeta_trace_wrong_backend.c
  "generic association|compatible type|controlling expression|C11 generic|not compatible with any association")
target_link_libraries(cmeta_trace_wrong_backend_compile_fail PRIVATE Salts::Platform)

cmeta_add_compile_failure_test(
  cmeta_fastpath_wrong_target_compile_fail
  compile_fail/cmeta_fastpath_wrong_target.c
  "generic association|compatible type|controlling expression|C11 generic|not compatible with any association")
target_link_libraries(cmeta_fastpath_wrong_target_compile_fail PRIVATE Salts::Platform)
cmeta_add_compile_failure_test(
  cmeta_fastpath_missing_decl_compile_fail
  compile_fail/cmeta_fastpath_missing_decl.c
  "undeclared_target_function_type|syntax error: identifier 'slot_target_type'")
target_link_libraries(cmeta_fastpath_missing_decl_compile_fail PRIVATE Salts::Platform)
