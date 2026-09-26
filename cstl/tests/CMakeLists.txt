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

# Minimal CMeta receiver-method lowering POC. The lowerer runs at build time;
# the generated executable contains only the direct typed CSTL call.
add_executable(cstl_method_lower_poc cstl_method_lower_poc.c)
target_link_libraries(cstl_method_lower_poc PRIVATE Salts::CMeta)
set_target_properties(cstl_method_lower_poc PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
  FOLDER "cstl/tests/poc")

set(CSTL_METHOD_LOWER_DIR "${CMAKE_CURRENT_BINARY_DIR}/method_lower")
set(CSTL_METHOD_LOWER_SOURCE "${CSTL_METHOD_LOWER_DIR}/list_add_lowered.c")
set(CSTL_METHOD_LOWER_FIXTURE
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/method_lower/list_add.cmeta")

add_custom_command(
  OUTPUT "${CSTL_METHOD_LOWER_SOURCE}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CSTL_METHOD_LOWER_DIR}"
  COMMAND $<TARGET_FILE:cstl_method_lower_poc>
          "${CSTL_METHOD_LOWER_FIXTURE}" "${CSTL_METHOD_LOWER_SOURCE}"
  DEPENDS cstl_method_lower_poc "${CSTL_METHOD_LOWER_FIXTURE}"
  COMMENT "Lowering list.add(10) to a direct typed CSTL call"
  VERBATIM)

add_executable(cstl_method_lowered_test "${CSTL_METHOD_LOWER_SOURCE}")
target_link_libraries(cstl_method_lowered_test PRIVATE Salts::CSTL)
set_target_properties(cstl_method_lowered_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
  FOLDER "cstl/tests/poc")
add_test(NAME cstl_method_lowered_test COMMAND cstl_method_lowered_test)

add_test(
  NAME cstl_method_lower_shape_test
  COMMAND ${CMAKE_COMMAND}
          -DGENERATED=${CSTL_METHOD_LOWER_SOURCE}
          -P "${CMAKE_CURRENT_SOURCE_DIR}/verify_method_lowering.cmake")

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
