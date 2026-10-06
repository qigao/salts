cmake_add_test(cmeta_fingerprint_test
  SOURCES cmeta_fingerprint_test.c cmeta_fingerprint_peer.c
  LIBS Salts::CMeta Salts::TinyTest FOLDER "cmeta/tests")
cmake_add_test(cmeta_fingerprint_cpp_test
  SOURCES cmeta_fingerprint_cpp_test.cpp cmeta_fingerprint_peer.c
  LIBS Salts::CMeta Salts::TinyTest FOLDER "cmeta/tests")
set_target_properties(cmeta_fingerprint_test cmeta_fingerprint_cpp_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_target_properties(cmeta_fingerprint_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

if(NOT BUILD_SHARED_LIBS AND NOT CMAKE_CROSSCOMPILING)
  add_library(cmeta_fingerprint_dso_fixture SHARED
    cmeta_fingerprint_dso_fixture.cpp cmeta_fingerprint_peer.c)
  target_link_libraries(cmeta_fingerprint_dso_fixture PRIVATE Salts::CMeta)
  target_compile_definitions(cmeta_fingerprint_dso_fixture PRIVATE
    CMETA_FINGERPRINT_DSO_TEST=1 CMETA_FINGERPRINT_DSO_BUILD=1)
  cmake_config_target(cmeta_fingerprint_dso_fixture FOLDER "cmeta/tests")
  set_target_properties(cmeta_fingerprint_dso_fixture PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  cmake_add_test(cmeta_fingerprint_dso_test SOURCES cmeta_fingerprint_dso_test.c
    LIBS cmeta_fingerprint_dso_fixture Salts::CMeta Salts::TinyTest FOLDER "cmeta/tests")
  target_compile_definitions(cmeta_fingerprint_dso_test PRIVATE CMETA_FINGERPRINT_DSO_TEST=1)
  set_target_properties(cmeta_fingerprint_dso_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cmeta_fingerprint_cpp_test PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-Werror=old-style-cast>")
endif()


# CMeta public API regression tests.

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

cmake_add_test(cmeta_execution_test
  SOURCES cmeta_execution_test.c
  LIBS Salts::CMeta Salts::Core Salts::CSTL Salts::Concurrency Salts::TinyTest
  FOLDER "cmeta/tests")
set_target_properties(cmeta_execution_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_tests_properties(cmeta_execution_test PROPERTIES TIMEOUT 60)
cmake_add_test(cmeta_capabilities_test
  SOURCES cmeta_capabilities_test.c cmeta_capabilities_peer.c cmeta_capabilities_owned.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_capabilities_header_cpp_test
  SOURCES cmeta_capabilities_header_cpp_test.cpp cmeta_capabilities_owned.c
  LIBS Salts::CMeta
  FOLDER "cmeta/tests")
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

add_executable(cmeta_function_admission_test cmeta_function_admission_test.c)
target_link_libraries(cmeta_function_admission_test PRIVATE Salts::CMeta Salts::TinyTest)
set_target_properties(cmeta_function_admission_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF FOLDER "cmeta/tests")
add_test(NAME cmeta_function_admission_test COMMAND cmeta_function_admission_test)

foreach(admission_case IN ITEMS POINTER_PARAM OBJECT_PARAM POINTER_RETURN OBJECT_RETURN VOID_ALIAS_RETURN)
  string(TOLOWER "${admission_case}" admission_name)
  set(admission_target "cmeta_function_${admission_name}_compile_fail")
  add_executable(${admission_target} EXCLUDE_FROM_ALL compile_fail/cmeta_function_inferred.c)
  target_link_libraries(${admission_target} PRIVATE Salts::CMeta)
  target_compile_definitions(${admission_target} PRIVATE "CMETA_TEST_${admission_case}=1")
  set_target_properties(${admission_target} PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF FOLDER "cmeta/tests/compile_fail")
  if(admission_case MATCHES "PARAM$")
    set(admission_diagnostic "CMeta three-field parameter input requires a registered scalar")
  else()
    set(admission_diagnostic "CMeta inferred return requires literal void or a registered scalar")
  endif()
  add_test(NAME ${admission_target}
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            --config $<CONFIG> --target ${admission_target})
  set_tests_properties(${admission_target} PROPERTIES
    PASS_REGULAR_EXPRESSION "${admission_diagnostic}" RUN_SERIAL TRUE)
endforeach()

cmake_add_test(cmeta_enum_bits_test
  SOURCES cmeta_enum_bits_test.c
  LIBS Salts::CMeta
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_core_test
  SOURCES cmeta_core_test.c cmeta_traits_peer.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_type_identity_test
  SOURCES cmeta_type_identity_test.c cmeta_type_identity_peer.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_declared_type_test
  SOURCES cmeta_declared_type_test.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

if(NOT CMAKE_CROSSCOMPILING)
  set(CMETA_INSPECTION_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/inspection")
  set(CMETA_INSPECTION_GENERATED_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/inspection-generated")
  set(CMETA_INSPECTION_GENERATED_SOURCE
      "${CMETA_INSPECTION_GENERATED_DIR}/cmeta_inspection_generated.c")

  add_library(cmeta_inspection_runtime STATIC
    inspection/cmeta_inspection_runtime.c)
  target_include_directories(cmeta_inspection_runtime PUBLIC
    "${CMETA_INSPECTION_TEST_DIR}")
  set_target_properties(cmeta_inspection_runtime PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    FOLDER "cmeta/tests/inspection")

  add_library(cmeta_inspection_metadata STATIC
    inspection/cmeta_inspection_metadata.c)
  target_include_directories(cmeta_inspection_metadata PUBLIC
    "${CMETA_INSPECTION_TEST_DIR}")
  target_link_libraries(cmeta_inspection_metadata PRIVATE Salts::CMeta)
  set_target_properties(cmeta_inspection_metadata PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    FOLDER "cmeta/tests/inspection")

  add_executable(cmeta_inspection_codegen
    inspection/cmeta_inspection_codegen.c)
  target_include_directories(cmeta_inspection_codegen PRIVATE
    "${CMETA_INSPECTION_TEST_DIR}")
  target_link_libraries(cmeta_inspection_codegen PRIVATE
    Salts::CMeta cmeta_inspection_metadata)
  set_target_properties(cmeta_inspection_codegen PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    FOLDER "cmeta/tests/inspection")

  add_custom_command(
    OUTPUT "${CMETA_INSPECTION_GENERATED_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMETA_INSPECTION_GENERATED_DIR}"
    COMMAND $<TARGET_FILE:cmeta_inspection_codegen>
            "${CMETA_INSPECTION_GENERATED_SOURCE}"
    DEPENDS cmeta_inspection_codegen
    VERBATIM)

  add_executable(cmeta_inspection_generated_test
    "${CMETA_INSPECTION_GENERATED_SOURCE}")
  target_include_directories(cmeta_inspection_generated_test PRIVATE
    "${CMETA_INSPECTION_TEST_DIR}")
  target_link_libraries(cmeta_inspection_generated_test PRIVATE
    cmeta_inspection_runtime)
  set_target_properties(cmeta_inspection_generated_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    FOLDER "cmeta/tests/inspection")
  add_test(NAME cmeta_inspection_generated_test
           COMMAND cmeta_inspection_generated_test)

endif()

cmake_add_test(cmeta_container_type_test
  SOURCES cmeta_container_type_test.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_data_test
  SOURCES cmeta_data_test.c cmeta_fixed_bytes_peer.c
          cmeta_fixed_array_test.c cmeta_fixed_array_peer.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_header_cpp_test
  SOURCES cmeta_header_cpp_test.cpp
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_meta_header_test
  SOURCES cmeta_meta_header_test.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_language_surface_test
  SOURCES cmeta_language_surface_test.c
  LIBS Salts::CMeta
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_scope_test
  SOURCES cmeta_scope_test.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_collector_test
  SOURCES cmeta_collector_test.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_signature_manifest_test
  SOURCES cmeta_signature_manifest_test.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_compute_test
  SOURCES cmeta_compute_test.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_infer_test
  SOURCES cmeta_infer_test.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_abi_test
  SOURCES cmeta_abi_test.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_manifest_test
  SOURCES cmeta_manifest_test.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

set_target_properties(cmeta_abi_test cmeta_manifest_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cmeta_function_reflection_test
  SOURCES cmeta_function_reflection_test.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_method_test
  SOURCES cmeta_method_test.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_invokable_test
  SOURCES cmeta_invokable_test.c cmeta_invokable_peer.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_object_test
  SOURCES cmeta_object_test.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")


cmake_add_test(cmeta_function_header_test
  SOURCES cmeta_function_header_test.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_interface_header_test
  SOURCES cmeta_interface_header_test.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

set_target_properties(
  cmeta_function_header_test
  cmeta_interface_header_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)


cmake_add_test(cmeta_interface_function_test
  SOURCES
    cmeta_interface_function_test.c
    cmeta_interface_function_peer_a.c
    cmeta_interface_function_peer_b.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

set_target_properties(cmeta_interface_function_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

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

cmeta_add_compile_failure_test(cmeta_capabilities_missing_trait_compile_fail
  compile_fail/cmeta_capabilities_missing_trait.c "CMeta required trait is missing")
cmeta_add_compile_failure_test(cmeta_capabilities_callback_compile_fail
  compile_fail/cmeta_capabilities_callback.c "CMeta trait callback signature mismatch")
cmeta_add_compile_failure_test(cmeta_capabilities_field_type_compile_fail
  compile_fail/cmeta_capabilities_field_type.c "CMeta required field type mismatch")
cmeta_add_compile_failure_test(cmeta_capabilities_field_qualifier_compile_fail
  compile_fail/cmeta_capabilities_field_qualifier.c "CMeta required field type mismatch")
cmeta_add_compile_failure_test(cmeta_capabilities_field_missing_compile_fail
  compile_fail/cmeta_capabilities_field_missing.c "absent_member")
cmeta_add_compile_failure_test(cmeta_capabilities_zero_tag_compile_fail
  compile_fail/cmeta_capabilities_zero_tag.c "CMeta variant tag must be nonzero and fit int")
cmeta_add_compile_failure_test(cmeta_capabilities_tag_range_compile_fail
  compile_fail/cmeta_capabilities_tag_range.c "CMeta variant tag must be nonzero and fit int")
cmeta_add_compile_failure_test(cmeta_capabilities_duplicate_tag_compile_fail
  compile_fail/cmeta_capabilities_duplicate_tag.c "duplicate case|case value.*already|case.*already")
cmeta_add_compile_failure_test(cmeta_capabilities_flags_type_compile_fail
  compile_fail/cmeta_capabilities_flags_type.c "OtherFlags|incompatible type for argument 2")

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

cmeta_add_compile_failure_test(
  cmeta_compute_missing_type_compile_fail
  compile_fail/cmeta_compute_missing_type.c
  "cmeta_type_fn_CMetaMissingType_1_absent")

cmeta_add_compile_failure_test(
  cmeta_compute_conflicting_value_compile_fail
  compile_fail/cmeta_compute_conflicting_value.c
  "cmeta_value_fn_CMetaConflictingValue_1_same")

cmeta_add_compile_failure_test(
  cmeta_compute_require_compile_fail
  compile_fail/cmeta_compute_require.c
  "CMeta predicate CMetaRejected rejects blocked")

cmeta_add_compile_failure_test(
  cmeta_intrusive_type_mismatch_compile_fail
  compile_fail/cmeta_intrusive_type_mismatch.c
  "CMeta intrusive member type mismatch")

cmeta_add_compile_failure_test(
  cmeta_scope_return_compile_fail
  compile_fail/cmeta_scope_return.c
  "syntax error.*return|expected expression")

cmeta_add_compile_failure_test(
  cmeta_scope_goto_compile_fail
  compile_fail/cmeta_scope_goto.c
  "syntax error.*goto|expected expression")

cmeta_add_compile_failure_test(
  cmeta_scope_break_compile_fail
  compile_fail/cmeta_scope_break.c
  "syntax error.*break|expected expression")

cmeta_add_compile_failure_test(
  cmeta_scope_cross_exit_compile_fail
  compile_fail/cmeta_scope_cross_exit.c
  "cmeta_scope_exit is removed")

cmeta_add_compile_failure_test(
  cmeta_scope_leave_compile_fail
  compile_fail/cmeta_scope_cross_exit.c
  "cmeta_leave is removed")
target_compile_definitions(cmeta_scope_leave_compile_fail PRIVATE CMETA_TEST_LEAVE=1)

cmeta_add_compile_failure_test(
  cmeta_scope_cross_function_goto_compile_fail
  compile_fail/cmeta_scope_cross_function_goto.c
  "outer__cmeta_cleanup")

set_target_properties(cmeta_header_cpp_test cmeta_capabilities_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cmeta_header_cpp_test PRIVATE
    -Werror=old-style-cast)
  target_compile_options(cmeta_capabilities_header_cpp_test PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Werror=old-style-cast>)
endif()

set_target_properties(cmeta_core_test cmeta_capabilities_test cmeta_capabilities_header_cpp_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

set_target_properties(cmeta_type_identity_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

set_target_properties(cmeta_declared_type_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

set_target_properties(cmeta_container_type_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

set_target_properties(cmeta_data_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  foreach(tgt cmeta_core_test cmeta_type_identity_test cmeta_declared_type_test cmeta_container_type_test cmeta_data_test cmeta_collector_test cmeta_function_reflection_test cmeta_method_test cmeta_object_test cmeta_function_header_test cmeta_interface_header_test cmeta_interface_function_test)
    target_compile_options(${tgt} PRIVATE -Werror=missing-field-initializers)
  endforeach()
endif()

set_target_properties(cmeta_meta_header_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

set_target_properties(cmeta_language_surface_test cmeta_scope_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

set_target_properties(cmeta_collector_test cmeta_signature_manifest_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

set_target_properties(cmeta_compute_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

set_target_properties(cmeta_infer_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

set_target_properties(cmeta_function_reflection_test cmeta_method_test cmeta_object_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cmeta_manifest_view_test
  SOURCES cmeta_manifest_view_test.c cmeta_manifest_view_peer.c
  LIBS Salts::CMeta Salts::TinyTest FOLDER "cmeta/tests")
cmake_add_test(cmeta_manifest_view_cpp_test
  SOURCES cmeta_manifest_view_cpp_test.cpp cmeta_manifest_view_peer.c
  LIBS Salts::CMeta Salts::TinyTest FOLDER "cmeta/tests")
set_target_properties(cmeta_manifest_view_test cmeta_manifest_view_cpp_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_target_properties(cmeta_manifest_view_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cmeta_manifest_view_cpp_test PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-Werror=old-style-cast>")
endif()
cmeta_add_compile_failure_test(cmeta_manifest_wrong_descriptor_compile_fail
  compile_fail/cmeta_manifest_wrong_descriptor.c
  "generic association|compatible type|controlling expression|C11 generic|not compatible with any association")

cmeta_add_compile_failure_test(cmeta_manifest_wrong_descriptor_cpp_compile_fail
  compile_fail/cmeta_manifest_wrong_descriptor.cpp "cmeta_type_desc")
set_target_properties(cmeta_manifest_wrong_descriptor_cpp_compile_fail PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

cmake_add_test(cmeta_plugin_test SOURCES cmeta_plugin_test.c cmeta_plugin_peer.c
  LIBS Salts::CMeta Salts::TinyTest FOLDER "cmeta/tests")
cmake_add_test(cmeta_plugin_cpp_test SOURCES cmeta_plugin_cpp_test.cpp cmeta_plugin_peer.c
  LIBS Salts::CMeta Salts::TinyTest FOLDER "cmeta/tests")
set_target_properties(cmeta_plugin_test cmeta_plugin_cpp_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_target_properties(cmeta_plugin_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cmeta_plugin_cpp_test PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-Werror=old-style-cast>")
endif()

cmeta_add_compile_failure_test(cmeta_plugin_wrong_descriptor_compile_fail
  compile_fail/cmeta_plugin_wrong_descriptor.c
  "generic association|compatible type|controlling expression|C11 generic|not compatible with any association")
cmeta_add_compile_failure_test(cmeta_plugin_wrong_descriptor_cpp_compile_fail
  compile_fail/cmeta_plugin_wrong_descriptor.cpp "cmeta_interface_desc")
set_target_properties(cmeta_plugin_wrong_descriptor_cpp_compile_fail PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

cmake_add_test(cmeta_pool_cpp_test SOURCES cmeta_pool_cpp_test.cpp
  LIBS Salts::CMeta Salts::Core Salts::TinyTest FOLDER "cmeta/tests")
set_target_properties(cmeta_pool_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
set_tests_properties(cmeta_pool_cpp_test PROPERTIES TIMEOUT 60)
