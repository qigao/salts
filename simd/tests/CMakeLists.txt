cmake_add_test(salts_simd_test
  SOURCES simd_test.c
  LIBS Salts::SIMD
  FOLDER "simd/tests")
set_target_properties(salts_simd_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(salts_simd_header_cpp_test
  SOURCES simd_header_cpp_test.cpp
  LIBS Salts::SIMD
  FOLDER "simd/tests")
set_target_properties(salts_simd_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
