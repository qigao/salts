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
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
