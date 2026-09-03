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
