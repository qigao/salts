# CMeta public API regression tests.

cmake_add_test(cmeta_core_test
  SOURCES cmeta_core_test.c cmeta_traits_peer.c
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_type_identity_test
  SOURCES cmeta_type_identity_test.c cmeta_type_identity_peer.c
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_declared_type_test
  SOURCES cmeta_declared_type_test.c
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_container_type_test
  SOURCES cmeta_container_type_test.c
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_data_test
  SOURCES cmeta_data_test.c
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_header_cpp_test
  SOURCES cmeta_header_cpp_test.cpp
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_meta_header_test
  SOURCES cmeta_meta_header_test.c
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_language_surface_test
  SOURCES cmeta_language_surface_test.c
  LIBS TurboUtils::CMeta
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_collector_test
  SOURCES cmeta_collector_test.c
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_signature_manifest_test
  SOURCES cmeta_signature_manifest_test.c
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_compute_test
  SOURCES cmeta_compute_test.c
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_infer_test
  SOURCES cmeta_infer_test.c
  LIBS TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "cmeta/tests")

function(cmeta_add_compile_failure_test target_name source expected_diagnostic)
  add_executable(${target_name} EXCLUDE_FROM_ALL ${source})
  target_link_libraries(${target_name} PRIVATE TurboUtils::CMeta)
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
  foreach(tgt cmeta_core_test cmeta_type_identity_test cmeta_declared_type_test cmeta_container_type_test cmeta_data_test cmeta_collector_test)
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
