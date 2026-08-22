set(CMETA_FORMAL_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${CMETA_FORMAL_GENERATED_DIR}")

# Backend-specific nested replay certification currently exists only for GNU and
# Clang preprocessors. MSVC participates in portable CMeta/CFlow conformance and
# Lean kernel builds, but does not claim a generated nested-replay certificate.
set(CMETA_FORMAL_BACKEND_CERTIFICATE OFF)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang")
  # Direct same-producer nesting is expected to fail in a strict C11
  # preprocessor because the active producer macro is suppressed during
  # replacement rescanning. Keep this as negative applicability evidence for the
  # certified GNU/Clang proof slice.
  try_compile(CMETA_DIRECT_NESTED_REPLAY_COMPILED
    "${CMAKE_CURRENT_BINARY_DIR}/direct-nested-replay-probe"
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/cmeta_nested_replay_direct_fail.c"
    CMAKE_FLAGS
      "-DCMAKE_C_STANDARD=11"
      "-DCMAKE_C_STANDARD_REQUIRED=ON"
      "-DCMAKE_C_EXTENSIONS=OFF"
    OUTPUT_VARIABLE CMETA_DIRECT_NESTED_REPLAY_OUTPUT)
  if(CMETA_DIRECT_NESTED_REPLAY_COMPILED)
    set(CMETA_DIRECT_NESTED_REPLAY_ACCEPTED_VALUE 1)
    message(FATAL_ERROR
      "direct same-producer nested replay unexpectedly compiled; self-suppression proof invalid")
  else()
    set(CMETA_DIRECT_NESTED_REPLAY_ACCEPTED_VALUE 0)
    message(STATUS "direct same-producer nested replay rejected as expected")
  endif()

  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(CMETA_NESTED_REPLAY_LEAN_NAMESPACE "CMeta.NestedReplayGccGeneratedC")
  else()
    set(CMETA_NESTED_REPLAY_LEAN_NAMESPACE "CMeta.NestedReplayClangGeneratedC")
  endif()
  set(CMETA_FORMAL_BACKEND_CERTIFICATE ON)
  message(STATUS
    "nested replay certificate namespace: ${CMETA_NESTED_REPLAY_LEAN_NAMESPACE}")
elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
  message(STATUS
    "MSVC portable formal conformance enabled; nested replay backend certificate skipped")
else()
  message(FATAL_ERROR
    "formal conformance supports GNU/Clang certification or MSVC portability, got ${CMAKE_C_COMPILER_ID}")
endif()

function(cmeta_add_formal_witness target source)
  add_executable(${target} EXCLUDE_FROM_ALL ${source})
  target_compile_features(${target} PRIVATE c_std_11)
  set_target_properties(${target} PROPERTIES FOLDER "formal")
endfunction()

function(cmeta_try_compile name source)
  try_compile(${name}_COMPILED
    "${CMAKE_CURRENT_BINARY_DIR}/${name}-probe"
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${source}"
    CMAKE_FLAGS
      "-DCMAKE_C_STANDARD=11"
      "-DCMAKE_C_STANDARD_REQUIRED=ON"
      "-DCMAKE_C_EXTENSIONS=OFF"
    COMPILE_DEFINITIONS
      "-I${PROJECT_SOURCE_DIR}/cmeta/include"
      "-I${PROJECT_SOURCE_DIR}/cflow/include"
    OUTPUT_VARIABLE ${name}_OUTPUT)
  set(${name}_COMPILED ${${name}_COMPILED} PARENT_SCOPE)
  set(${name}_OUTPUT "${${name}_OUTPUT}" PARENT_SCOPE)
endfunction()

function(cmeta_expect_compile_success name source)
  cmeta_try_compile(${name} ${source})
  if(NOT ${name}_COMPILED)
    message(FATAL_ERROR
      "${name} unexpectedly failed to compile:\n${${name}_OUTPUT}")
  endif()
endfunction()

function(cmeta_expect_compile_failure name source)
  cmeta_try_compile(${name} ${source})
  if(${name}_COMPILED)
    message(FATAL_ERROR "${name} unexpectedly compiled")
  endif()
endfunction()

cmeta_expect_compile_success(CMETA_TRAITS_VALID
  cmeta_traits_row_syntax_witness.c)
cmeta_expect_compile_failure(CMETA_TRAITS_DUPLICATE_TAG
  cmeta_traits_duplicate_tag_fail.c)
cmeta_expect_compile_failure(CMETA_TRAITS_UNKNOWN_TAG
  cmeta_traits_unknown_tag_fail.c)
cmeta_expect_compile_failure(CMETA_TRAITS_MALFORMED_ROW
  cmeta_traits_malformed_row_fail.c)
cmeta_expect_compile_failure(CMETA_OPERATOR_UNKNOWN_TAG
  cmeta_operator_unknown_tag_fail.c)
cmeta_expect_compile_failure(CMETA_OPERATOR_DUPLICATE_SUBROW
  cmeta_operator_duplicate_subrow_fail.c)
cmeta_expect_compile_failure(CMETA_OPERATOR_MISSING_SUBROW
  cmeta_operator_missing_subrow_fail.c)
cmeta_expect_compile_failure(CMETA_OPERATOR_MALFORMED_SUBROW
  cmeta_operator_malformed_subrow_fail.c)

cmeta_add_formal_witness(cmeta_header_conformance_witness
  cmeta_conformance_witness.c)
target_include_directories(cmeta_header_conformance_witness PRIVATE
  ${PROJECT_SOURCE_DIR}/cmeta/include
  ${PROJECT_SOURCE_DIR}/cflow/include)

cmeta_add_formal_witness(cmeta_type_identity_conformance_witness
  cmeta_type_identity_conformance_witness.c)
target_link_libraries(cmeta_type_identity_conformance_witness PRIVATE TurboUtils::CMeta)
add_test(NAME cmeta_type_identity_conformance
         COMMAND cmeta_type_identity_conformance_witness)

cmeta_add_formal_witness(cmeta_descriptor_bridge_conformance_witness
  cmeta_descriptor_bridge_conformance_witness.c)
target_link_libraries(cmeta_descriptor_bridge_conformance_witness PRIVATE TurboUtils::CMeta)
add_test(NAME cmeta_descriptor_bridge_conformance
         COMMAND cmeta_descriptor_bridge_conformance_witness)

add_executable(cmeta_type_identity_multi_tu EXCLUDE_FROM_ALL
  type_identity_multi_tu.c
  type_identity_tu_a.c
  type_identity_tu_b.c)
target_compile_features(cmeta_type_identity_multi_tu PRIVATE c_std_11)
target_link_libraries(cmeta_type_identity_multi_tu PRIVATE TurboUtils::CMeta)
set_target_properties(cmeta_type_identity_multi_tu PROPERTIES FOLDER "formal")
add_test(NAME cmeta_type_identity_multi_tu COMMAND cmeta_type_identity_multi_tu)

add_executable(cmeta_type_universe_probe EXCLUDE_FROM_ALL
  cmeta_type_universe_probe.c)
target_compile_features(cmeta_type_universe_probe PRIVATE c_std_11)
target_include_directories(cmeta_type_universe_probe PRIVATE
  ${PROJECT_SOURCE_DIR}/cmeta/include)
set_target_properties(cmeta_type_universe_probe PROPERTIES FOLDER "formal")
add_test(NAME cmeta_type_universe_probe COMMAND cmeta_type_universe_probe)

cmeta_add_formal_witness(cmeta_traits_row_syntax_witness
  cmeta_traits_row_syntax_witness.c)
target_include_directories(cmeta_traits_row_syntax_witness PRIVATE
  ${PROJECT_SOURCE_DIR}/cmeta/include)
add_test(NAME cmeta_traits_row_syntax COMMAND cmeta_traits_row_syntax_witness)

cmeta_add_formal_witness(cmeta_operator_row_syntax_witness
  cmeta_operator_row_syntax_witness.c)
target_include_directories(cmeta_operator_row_syntax_witness PRIVATE
  ${PROJECT_SOURCE_DIR}/cmeta/include)
add_test(NAME cmeta_operator_row_syntax COMMAND cmeta_operator_row_syntax_witness)

cmeta_add_formal_witness(cmeta_fmt_args_simplification_witness
  fmt_args_simplification_witness.c)
target_include_directories(cmeta_fmt_args_simplification_witness PRIVATE
  ${PROJECT_SOURCE_DIR}/utils/include
  ${PROJECT_SOURCE_DIR}/utils/parser
  ${PROJECT_SOURCE_DIR}/cmeta/include)
add_test(NAME cmeta_fmt_args_simplification
         COMMAND cmeta_fmt_args_simplification_witness)

cmeta_add_formal_witness(cmeta_producer_replay_witness
  cmeta_producer_replay_witness.c)
target_include_directories(cmeta_producer_replay_witness PRIVATE
  ${PROJECT_SOURCE_DIR}/cmeta/include)
set_target_properties(cmeta_producer_replay_witness PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
  C_EXTENSIONS NO)
add_test(NAME cmeta_producer_replay
         COMMAND cmeta_producer_replay_witness)

if(CMETA_FORMAL_BACKEND_CERTIFICATE)
  cmeta_add_formal_witness(cmeta_nested_replay_deferred_witness
    cmeta_nested_replay_deferred_witness.c)
  target_include_directories(cmeta_nested_replay_deferred_witness PRIVATE
    ${PROJECT_SOURCE_DIR}/cmeta/include)
  target_compile_definitions(cmeta_nested_replay_deferred_witness PRIVATE
    CMETA_DIRECT_NESTED_REPLAY_ACCEPTED=${CMETA_DIRECT_NESTED_REPLAY_ACCEPTED_VALUE}
    CMETA_NESTED_REPLAY_LEAN_NAMESPACE="${CMETA_NESTED_REPLAY_LEAN_NAMESPACE}")
  set_target_properties(cmeta_nested_replay_deferred_witness PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO)
  add_test(NAME cmeta_nested_replay_deferred
           COMMAND cmeta_nested_replay_deferred_witness)
endif()

cmeta_add_formal_witness(cmeta_plan_conformance_witness
  cmeta_plan_conformance_witness.c)
target_link_libraries(cmeta_plan_conformance_witness PRIVATE TurboUtils::CFlow)

cmeta_add_formal_witness(cmeta_structured_conformance_witness
  cmeta_structured_conformance_witness.c)
target_link_libraries(cmeta_structured_conformance_witness PRIVATE TurboUtils::CFlow)

cmeta_add_formal_witness(cmeta_structured_policy_conformance_witness
  cmeta_structured_policy_conformance_witness.c)
target_link_libraries(cmeta_structured_policy_conformance_witness PRIVATE TurboUtils::CFlow)

cmeta_add_formal_witness(cmeta_optimizer_conformance_witness
  cmeta_optimizer_conformance_witness.c)
target_link_libraries(cmeta_optimizer_conformance_witness PRIVATE TurboUtils::CFlow)

cmeta_add_formal_witness(cmeta_optimizer_gating_conformance_witness
  cmeta_optimizer_gating_conformance_witness.c)
target_link_libraries(cmeta_optimizer_gating_conformance_witness PRIVATE TurboUtils::CFlow)

cmeta_add_formal_witness(cmeta_optimizer_topology_conformance_witness
  cmeta_optimizer_topology_conformance_witness.c)
target_link_libraries(cmeta_optimizer_topology_conformance_witness PRIVATE TurboUtils::CFlow)
