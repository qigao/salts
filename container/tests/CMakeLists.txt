cmake_add_test(container_header_test
  SOURCES container_header_test.c
  LIBS TurboUtils::Container TurboUtils::TinyTest
  FOLDER "container/tests")

cmake_add_test(container_header_cpp_test
  SOURCES container_header_cpp_test.cpp
  LIBS TurboUtils::Container TurboUtils::TinyTest
  FOLDER "container/tests")

cmake_add_test(container_header_typed_test
  SOURCES container_header_typed_test.c
  LIBS TurboUtils::Container TurboUtils::TinyTest
  FOLDER "container/tests")

cmake_add_test(container_header_typed_cpp_test
  SOURCES container_header_typed_cpp_test.cpp
  LIBS TurboUtils::Container TurboUtils::TinyTest
  FOLDER "container/tests")

cmake_add_test(container_sequence_test
  SOURCES container_sequence_test.c
  LIBS TurboUtils::Container TurboUtils::TinyTest
  FOLDER "container/tests")

cmake_add_test(container_ownership_test
  SOURCES container_ownership_test.c
  LIBS TurboUtils::Container TurboUtils::TinyTest
  FOLDER "container/tests")

cmake_add_test(container_hash_test
  SOURCES container_hash_test.c
  LIBS TurboUtils::Container TurboUtils::TinyTest
  FOLDER "container/tests")

cmake_add_test(container_typed_test
  SOURCES container_typed_test.c
  LIBS TurboUtils::Container TurboUtils::TinyTest
  FOLDER "container/tests")

cmake_add_test(container_tree_test
  SOURCES container_tree_test.c
  LIBS TurboUtils::Container TurboUtils::TinyTest
  FOLDER "container/tests")

cmake_add_test(container_entry_test
  SOURCES container_entry_test.c
  LIBS TurboUtils::Container TurboUtils::TinyTest
  FOLDER "container/tests")

cmake_add_test(container_sort_test
  SOURCES container_sort_test.c
  LIBS TurboUtils::Container TurboUtils::TinyTest
  FOLDER "container/tests")

try_compile(container_typed_unregistered_kind_compiles
  "${CMAKE_CURRENT_BINARY_DIR}/typed_unregistered_kind_probe"
  SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/container_typed_negative_unregistered.c"
  CMAKE_FLAGS
    "-DCMAKE_C_STANDARD=11"
    "-DCMAKE_C_STANDARD_REQUIRED=ON"
    "-DINCLUDE_DIRECTORIES:STRING=${CMAKE_SOURCE_DIR}/container/include;${CMAKE_SOURCE_DIR}/cmeta/include"
  OUTPUT_VARIABLE container_typed_unregistered_kind_output)

if(container_typed_unregistered_kind_compiles)
  message(FATAL_ERROR
    "Container typed schema accepted an unregistered container kind")
endif()

if(NOT container_typed_unregistered_kind_output MATCHES
   "CMETA_TYPED_UNREGISTERED_NotAContainerKind")
  message(FATAL_ERROR
    "Container typed negative probe failed without its stable diagnostic token:\n"
    "${container_typed_unregistered_kind_output}")
endif()

set_target_properties(
  container_header_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

set_target_properties(
  container_header_cpp_test
  PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

set_target_properties(
  container_header_typed_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

set_target_properties(
  container_header_typed_cpp_test
  PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

set_target_properties(
  container_sequence_test
  container_ownership_test
  container_hash_test
  container_typed_test
  container_tree_test
  container_entry_test
  container_sort_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
