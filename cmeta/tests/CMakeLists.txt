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

set_target_properties(cmeta_collector_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
