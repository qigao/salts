cmake_add_test(cstl_header_test
  SOURCES cstl_header_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_header_cpp_test
  SOURCES cstl_header_cpp_test.cpp
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_header_typed_test
  SOURCES cstl_header_typed_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_imported_metadata_test
  SOURCES cstl_imported_metadata_test.c
  LIBS Salts::Core Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_tinytest_test
  SOURCES cstl_tinytest_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_header_typed_cpp_test
  SOURCES cstl_header_typed_cpp_test.cpp
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_stack_type_collision_test
  SOURCES cstl_stack_type_collision_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_generic_identity_test
  SOURCES cstl_generic_identity_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_semantic_projection_test
  SOURCES cstl_semantic_projection_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_construction_binding_test
  SOURCES cstl_construction_binding_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_sequence_test
  SOURCES cstl_sequence_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_ownership_test
  SOURCES cstl_ownership_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_hash_test
  SOURCES cstl_hash_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_typed_test
  SOURCES cstl_typed_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

# Production CMeta source-lowering frontend integration.
# cmeta-lower is a host tool; target cross-builds never execute it.
if(NOT CMAKE_CROSSCOMPILING)
  set(CSTL_CMETA_LOWER_DIR "${CMAKE_CURRENT_BINARY_DIR}/cmeta_lower")
  set(CSTL_CMETA_LOWER_INPUT
      "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cmeta_lower/containers.cmeta.c")
  set(CSTL_CMETA_LOWER_OUTPUT
      "${CSTL_CMETA_LOWER_DIR}/cstl_cmeta_lower_runtime.c")

  salts_cmeta_lower_source(
    INPUT "${CSTL_CMETA_LOWER_INPUT}"
    OUTPUT "${CSTL_CMETA_LOWER_OUTPUT}")

  add_executable(cstl_cmeta_lower_runtime "${CSTL_CMETA_LOWER_OUTPUT}")
  target_link_libraries(cstl_cmeta_lower_runtime PRIVATE Salts::CSTL)
  set_target_properties(cstl_cmeta_lower_runtime PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    FOLDER "cstl/tests/cmeta-lower")

  add_test(NAME cstl_cmeta_lower_runtime COMMAND cstl_cmeta_lower_runtime)
  add_test(
    NAME cstl_cmeta_lower_shape
    COMMAND ${CMAKE_COMMAND}
            -DGENERATED=${CSTL_CMETA_LOWER_OUTPUT}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_cmeta_lowering.cmake")
  add_test(
    NAME cmeta_lower_version
    COMMAND $<TARGET_FILE:cmeta_lower> --version)

  function(cstl_add_cmeta_lower_rejection_test name fixture diagnostic)
    add_test(
      NAME ${name}
      COMMAND ${CMAKE_COMMAND}
              -DTOOL=$<TARGET_FILE:cmeta_lower>
              -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cmeta_lower/${fixture}
              -DOUTPUT=${CSTL_CMETA_LOWER_DIR}/${name}.c
              "-DEXPECTED=${diagnostic}"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_cmeta_lower_rejection.cmake")
  endfunction()

  cstl_add_cmeta_lower_rejection_test(
    cstl_cmeta_lower_wrong_owner
    wrong_owner.cmeta.c
    "generic owner 'Vec' does not match IntList owner 'List'")
  cstl_add_cmeta_lower_rejection_test(
    cstl_cmeta_lower_unknown_receiver
    unknown_receiver.cmeta.c
    "unknown typed receiver 'missing'")
endif()


cmake_add_test(cstl_tree_test
  SOURCES cstl_tree_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_list_test
  SOURCES cstl_list_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_map_test
  SOURCES cstl_map_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  foreach(tgt cstl_list_test cstl_map_test)
    target_compile_options(${tgt} PRIVATE -Werror=misleading-indentation)
  endforeach()
  target_compile_options(cstl_map_test PRIVATE
    -Werror=missing-field-initializers)
endif()

cmake_add_test(cstl_rbtree_test
  SOURCES cstl_rbtree_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_entry_test
  SOURCES cstl_entry_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_sort_test
  SOURCES cstl_sort_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_stream_test
  SOURCES cstl_stream_test.c
  LIBS Salts::CSTLStream Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_stream_cpp_test
  SOURCES cstl_stream_cpp_test.cpp
  LIBS Salts::CSTLStream Salts::TinyTest
  FOLDER "cstl/tests")

cmake_add_test(cstl_managed_stream_test
  SOURCES cstl_managed_stream_test.c
  LIBS Salts::CSTLStream Salts::TinyTest
  FOLDER "cstl/tests")

set_target_properties(
  cstl_header_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

set_target_properties(cstl_stream_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)

set_target_properties(
  cstl_header_cpp_test
  PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

set_target_properties(
  cstl_header_typed_test
  cstl_imported_metadata_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

set_target_properties(
  cstl_header_typed_cpp_test
  PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

set_target_properties(
  cstl_stack_type_collision_test
  cstl_generic_identity_test
  cstl_semantic_projection_test
  cstl_construction_binding_test
  cstl_sequence_test
  cstl_ownership_test
  cstl_hash_test
  cstl_typed_test
  cstl_tinytest_test
  cstl_tree_test
  cstl_list_test
  cstl_map_test
  cstl_rbtree_test
  cstl_entry_test
  cstl_sort_test
  cstl_stream_test
  cstl_managed_stream_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

cmake_add_test(cstl_vec_alloc_test
  SOURCES cstl_vec_alloc_test.c
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")
cmake_add_test(cstl_vec_alloc_cpp_test
  SOURCES cstl_vec_alloc_cpp_test.cpp
  LIBS Salts::CSTL Salts::TinyTest
  FOLDER "cstl/tests")
set_target_properties(cstl_vec_alloc_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_target_properties(cstl_vec_alloc_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
