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

try_compile(turbostl_typed_unregistered_kind_compiles
  "${CMAKE_CURRENT_BINARY_DIR}/typed_unregistered_kind_probe"
  SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/turbostl_typed_negative_unregistered.c"
  CMAKE_FLAGS
    "-DCMAKE_C_STANDARD=11"
    "-DCMAKE_C_STANDARD_REQUIRED=ON"
    "-DINCLUDE_DIRECTORIES:STRING=${CMAKE_SOURCE_DIR}/turbostl/include;${CMAKE_SOURCE_DIR}/cmeta/include"
  OUTPUT_VARIABLE turbostl_typed_unregistered_kind_output)

if(turbostl_typed_unregistered_kind_compiles)
  message(FATAL_ERROR
    "TurboSTL typed schema accepted an unregistered container kind")
endif()

if(NOT turbostl_typed_unregistered_kind_output MATCHES
   "CMETA_TYPED_UNREGISTERED_NotAContainerKind")
  message(FATAL_ERROR
    "TurboSTL typed negative probe failed without its stable diagnostic token:\n"
    "${turbostl_typed_unregistered_kind_output}")
endif()

unset(turbostl_list_forbidden_api_compiles CACHE)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
try_compile(turbostl_list_forbidden_api_compiles
  "${CMAKE_CURRENT_BINARY_DIR}/list_forbidden_api_probe"
  SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/turbostl_list_negative_capacity.c"
  CMAKE_FLAGS
    "-DCMAKE_C_STANDARD=11"
    "-DCMAKE_C_STANDARD_REQUIRED=ON"
    "-DINCLUDE_DIRECTORIES:STRING=${CMAKE_SOURCE_DIR}/turbostl/include;${CMAKE_SOURCE_DIR}/cmeta/include"
  OUTPUT_VARIABLE turbostl_list_forbidden_api_output)
unset(CMAKE_TRY_COMPILE_TARGET_TYPE)

if(turbostl_list_forbidden_api_compiles)
  message(FATAL_ERROR
    "List still exposes forbidden reserve/capacity/at APIs")
endif()

if(NOT turbostl_list_forbidden_api_output MATCHES
   "turbo_list_(reserve|capacity|at)")
  message(FATAL_ERROR
    "List forbidden-API probe failed without an expected symbol diagnostic:\n"
    "${turbostl_list_forbidden_api_output}")
endif()

unset(turbostl_map_forbidden_api_compiles CACHE)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
try_compile(turbostl_map_forbidden_api_compiles
  "${CMAKE_CURRENT_BINARY_DIR}/map_forbidden_api_probe"
  SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/turbostl_map_negative_index.c"
  CMAKE_FLAGS
    "-DCMAKE_C_STANDARD=11"
    "-DCMAKE_C_STANDARD_REQUIRED=ON"
    "-DINCLUDE_DIRECTORIES:STRING=${CMAKE_SOURCE_DIR}/turbostl/include;${CMAKE_SOURCE_DIR}/cmeta/include"
  OUTPUT_VARIABLE turbostl_map_forbidden_api_output)
unset(CMAKE_TRY_COMPILE_TARGET_TYPE)

if(turbostl_map_forbidden_api_compiles)
  message(FATAL_ERROR
    "Map still exposes forbidden reserve/capacity/key_at APIs")
endif()

if(NOT turbostl_map_forbidden_api_output MATCHES
   "turbo_map_(reserve|capacity|key_at)")
  message(FATAL_ERROR
    "Map forbidden-API probe failed without an expected symbol diagnostic:\n"
    "${turbostl_map_forbidden_api_output}")
endif()

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
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
