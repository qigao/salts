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

# CMeta receiver-method lowering POC. The lowerer is a host build-time
# tool, so do not try to execute a target binary while cross-compiling.
if(NOT CMAKE_CROSSCOMPILING)
add_executable(cstl_method_lower_poc cstl_method_lower_poc.c)
target_link_libraries(cstl_method_lower_poc PRIVATE Salts::CSTL)
set_target_properties(cstl_method_lower_poc PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
  FOLDER "cstl/tests/poc")

set(CSTL_METHOD_LOWER_DIR "${CMAKE_CURRENT_BINARY_DIR}/method_lower")

function(cstl_add_method_lowering_case name fixture expected_call)
  set(lowered_source "${CSTL_METHOD_LOWER_DIR}/${name}.c")
  add_custom_command(
    OUTPUT "${lowered_source}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CSTL_METHOD_LOWER_DIR}"
    COMMAND $<TARGET_FILE:cstl_method_lower_poc>
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/method_lower/${fixture}"
            "${lowered_source}"
    DEPENDS cstl_method_lower_poc
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/method_lower/${fixture}"
    COMMENT "Lowering ${fixture} to a direct typed CSTL call"
    VERBATIM)

  add_executable(${name} "${lowered_source}")
  target_link_libraries(${name} PRIVATE Salts::CSTL)
  set_target_properties(${name} PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    FOLDER "cstl/tests/poc")

  add_test(NAME ${name} COMMAND ${name})
  add_test(
    NAME ${name}_shape
    COMMAND ${CMAKE_COMMAND}
            -DGENERATED=${lowered_source}
            "-DEXPECTED_CALL=${expected_call}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_method_lowering.cmake")
endfunction()

cstl_add_method_lowering_case(
  cstl_method_vec_push
  vec_push.cmeta
  "rc = IntVec_push(&vec, 10)")
cstl_add_method_lowering_case(
  cstl_method_list_add
  list_add.cmeta
  "rc = IntList_add(&list, 10)")
cstl_add_method_lowering_case(
  cstl_method_set_add
  set_add.cmeta
  "rc = IntSet_add(&set, 10)")
cstl_add_method_lowering_case(
  cstl_method_map_put
  map_put.cmeta
  "rc = IntMap_put(&map, 1, 20)")

function(cstl_add_method_rejection_test name fixture diagnostic)
  add_test(
    NAME ${name}
    COMMAND ${CMAKE_COMMAND}
            -DTOOL=$<TARGET_FILE:cstl_method_lower_poc>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/method_lower/${fixture}
            -DOUTPUT=${CSTL_METHOD_LOWER_DIR}/${name}.c
            "-DEXPECTED=${diagnostic}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_method_rejection.cmake")
endfunction()

cstl_add_method_rejection_test(
  cstl_method_lower_unknown_method_test
  unknown_method.cmeta
  "IntList has no receiver method 'unknown'")
cstl_add_method_rejection_test(
  cstl_method_lower_wrong_receiver_test
  wrong_receiver.cmeta
  "unknown receiver 'bogus'")
cstl_add_method_rejection_test(
  cstl_method_lower_wrong_argument_test
  wrong_argument.cmeta
  "list.add arguments do not match reflected signature")
cstl_add_method_rejection_test(
  cstl_method_lower_wrong_arity_test
  wrong_arity.cmeta
  "map.put arguments do not match reflected signature")
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
