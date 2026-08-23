cmake_add_test(turbostl_header_test
  SOURCES turbostl_header_test.c
  LIBS TurboUtils::STL TurboUtils::TinyTest
  FOLDER "turbostl/tests")

cmake_add_test(turbostl_header_cpp_test
  SOURCES turbostl_header_cpp_test.cpp
  LIBS TurboUtils::STL TurboUtils::TinyTest
  FOLDER "turbostl/tests")

cmake_add_test(turbostl_header_typed_test
  SOURCES turbostl_header_typed_test.c
  LIBS TurboUtils::STL TurboUtils::TinyTest
  FOLDER "turbostl/tests")

cmake_add_test(turbostl_header_typed_cpp_test
  SOURCES turbostl_header_typed_cpp_test.cpp
  LIBS TurboUtils::STL TurboUtils::TinyTest
  FOLDER "turbostl/tests")

cmake_add_test(turbostl_generic_identity_test
  SOURCES turbostl_generic_identity_test.c
  LIBS TurboUtils::STL TurboUtils::TinyTest
  FOLDER "turbostl/tests")

cmake_add_test(turbostl_semantic_data_test
  SOURCES turbostl_semantic_data_test.c
  LIBS TurboUtils::STL TurboUtils::CMeta TurboUtils::TinyTest
  FOLDER "turbostl/tests")

cmake_add_test(turbostl_sequence_test
  SOURCES turbostl_sequence_test.c
  LIBS TurboUtils::STL TurboUtils::TinyTest
  FOLDER "turbostl/tests")

cmake_add_test(turbostl_ownership_test
  SOURCES turbostl_ownership_test.c
  LIBS TurboUtils::STL TurboUtils::TinyTest
  FOLDER "turbostl/tests")

cmake_add_test(turbostl_hash_test
  SOURCES turbostl_hash_test.c
  LIBS TurboUtils::STL TurboUtils::TinyTest
  FOLDER "turbostl/tests")

cmake_add_test(turbostl_typed_test
  SOURCES turbostl_typed_test.c
  LIBS TurboUtils::STL TurboUtils::TinyTest
  FOLDER "turbostl/tests")

cmake_add_test(turbostl_tree_test
  SOURCES turbostl_tree_test.c
  LIBS TurboUtils::STL TurboUtils::TinyTest
  FOLDER "turbostl/tests")

cmake_add_test(turbostl_list_test
  SOURCES turbostl_list_test.c
  LIBS TurboUtils::STL TurboUtils::TinyTest
  FOLDER "turbostl/tests")

cmake_add_test(turbostl_map_test
  SOURCES turbostl_map_test.c
  LIBS TurboUtils::STL TurboUtils::TinyTest
  FOLDER "turbostl/tests")

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  foreach(tgt turbostl_list_test turbostl_map_test)
    target_compile_options(${tgt} PRIVATE -Werror=misleading-indentation)
  endforeach()
  target_compile_options(turbostl_map_test PRIVATE
    -Werror=missing-field-initializers)
endif()

cmake_add_test(turbostl_rbtree_test
  SOURCES turbostl_rbtree_test.c
  LIBS TurboUtils::STL TurboUtils::TinyTest
  FOLDER "turbostl/tests")

cmake_add_test(turbostl_entry_test
  SOURCES turbostl_entry_test.c
  LIBS TurboUtils::STL TurboUtils::TinyTest
  FOLDER "turbostl/tests")

cmake_add_test(turbostl_sort_test
  SOURCES turbostl_sort_test.c
  LIBS TurboUtils::STL TurboUtils::TinyTest
  FOLDER "turbostl/tests")

cmake_add_test(turbostl_stream_test
  SOURCES turbostl_stream_test.c
  LIBS TurboUtils::STLStream TurboUtils::TinyTest
  FOLDER "turbostl/tests")

set_target_properties(
  turbostl_header_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

set_target_properties(
  turbostl_header_cpp_test
  PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

set_target_properties(
  turbostl_header_typed_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

set_target_properties(
  turbostl_header_typed_cpp_test
  PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

set_target_properties(
  turbostl_generic_identity_test
  turbostl_semantic_data_test
  turbostl_sequence_test
  turbostl_ownership_test
  turbostl_hash_test
  turbostl_typed_test
  turbostl_tree_test
  turbostl_list_test
  turbostl_map_test
  turbostl_rbtree_test
  turbostl_entry_test
  turbostl_sort_test
  turbostl_stream_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)