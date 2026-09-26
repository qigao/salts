# CMeta public API regression tests.

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

# libuv is already a manifest dependency. It is used only by the test host;
# CMeta and the provider remain independent of the platform loader.
find_package(libuv CONFIG REQUIRED)
add_library(cmeta_reflection_plugin MODULE cmeta_reflection_plugin.c)
target_link_libraries(cmeta_reflection_plugin PRIVATE Salts::CMeta)
set_target_properties(cmeta_reflection_plugin PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF FOLDER "cmeta/tests")
add_executable(cmeta_reflection_plugin_test cmeta_reflection_plugin_test.c)
target_link_libraries(cmeta_reflection_plugin_test PRIVATE
  Salts::CMeta Salts::TinyTest
  $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>)
target_compile_definitions(cmeta_reflection_plugin_test PRIVATE
  CMETA_TEST_PLUGIN_PATH="$<TARGET_FILE:cmeta_reflection_plugin>")
# uv.h exposes pthread_rwlock_t even when only its loader API is used.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_compile_definitions(cmeta_reflection_plugin_test PRIVATE _POSIX_C_SOURCE=200809L)
endif()
add_dependencies(cmeta_reflection_plugin_test cmeta_reflection_plugin)
set_target_properties(cmeta_reflection_plugin_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF FOLDER "cmeta/tests")
add_test(NAME cmeta_reflection_plugin_test COMMAND cmeta_reflection_plugin_test)

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

cmake_add_test(cmeta_container_type_test
  SOURCES cmeta_container_type_test.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_data_test
  SOURCES cmeta_data_test.c cmeta_fixed_bytes_peer.c
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

set_target_properties(cmeta_abi_test PROPERTIES
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

set_target_properties(cmeta_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cmeta_header_cpp_test PRIVATE
    -Werror=old-style-cast)
endif()

set_target_properties(cmeta_core_test PROPERTIES
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

set_target_properties(cmeta_language_surface_test PROPERTIES
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
