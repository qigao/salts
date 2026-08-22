# TinyTest self-tests and output/diagnostic executables.
#
# The public headers and static runtime are provided through TurboUtils::TinyTest,
# so each target only lists the test source it owns.

cmake_add_executable(math_test
  SOURCES math_test.c
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

if(UNIX)
  target_link_libraries(math_test PRIVATE m)
endif()

cmake_add_executable(test_tree_test
  SOURCES test-tree.c test-tree.h
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

cmake_add_executable(array_test
  SOURCES array.c array.h
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

cmake_add_executable(dynamic_test
  SOURCES dynamic-test.c array.h
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

cmake_add_executable(before_after
  SOURCES before-after.c
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

cmake_add_test(tinytest_regression_fixes
  SOURCES regression_fixes.c
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

cmake_add_test(tinytest_regression_runtime
  SOURCES regression_runtime.c
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

cmake_add_test(c11_generic_test
  SOURCES c11_generic_test.c
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

cmake_add_test(tinytest_cmeta_equal_test
  SOURCES cmeta_equal_test.c
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

# Negative language probes must be compile-only. Otherwise an unrelated link
# failure can be mistaken for the expected compile-time diagnostic.
set(_tinytest_saved_try_compile_target_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# TinyTest's C surface is C11. Keep this probe on the same baseline rather than
# silently testing a newer C17 dialect.
try_compile(tinytest_cmeta_unregistered_compiles
  "${CMAKE_CURRENT_BINARY_DIR}/cmeta_equal_unregistered_probe"
  SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/cmeta_equal_unregistered.c"
  CMAKE_FLAGS
    "-DCMAKE_C_STANDARD=11"
    "-DCMAKE_C_STANDARD_REQUIRED=ON"
    "-DCMAKE_C_EXTENSIONS=OFF"
    "-DINCLUDE_DIRECTORIES:STRING=${CMAKE_SOURCE_DIR}/tinytest/include"
  OUTPUT_VARIABLE tinytest_cmeta_unregistered_output)

if(tinytest_cmeta_unregistered_compiles)
  message(FATAL_ERROR
    "TinyTest accepted check_equal for an unregistered structure")
endif()

if(NOT WIN32)
  # CMP0067 makes source-signature try_compile inherit the caller's language
  # standard. Override the caller locally so this is a real C99/pre-C11 probe.
  set(_tinytest_saved_c_standard "${CMAKE_C_STANDARD}")
  set(_tinytest_saved_c_standard_required "${CMAKE_C_STANDARD_REQUIRED}")
  set(_tinytest_saved_c_extensions "${CMAKE_C_EXTENSIONS}")
  set(CMAKE_C_STANDARD 99)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_C_EXTENSIONS OFF)

  try_compile(tinytest_pre_c11_compiles
    "${CMAKE_CURRENT_BINARY_DIR}/pre_c11_compile_probe"
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/pre_c11_compile_fail.c"
    CMAKE_FLAGS
      "-DINCLUDE_DIRECTORIES:STRING=${CMAKE_SOURCE_DIR}/tinytest/include"
    OUTPUT_VARIABLE tinytest_pre_c11_output)

  set(CMAKE_C_STANDARD "${_tinytest_saved_c_standard}")
  set(CMAKE_C_STANDARD_REQUIRED "${_tinytest_saved_c_standard_required}")
  set(CMAKE_C_EXTENSIONS "${_tinytest_saved_c_extensions}")
  unset(_tinytest_saved_c_standard)
  unset(_tinytest_saved_c_standard_required)
  unset(_tinytest_saved_c_extensions)

  if(tinytest_pre_c11_compiles)
    message(FATAL_ERROR "tinytest.h accepted a pre-C11 C translation unit")
  endif()

  if(NOT tinytest_pre_c11_output MATCHES "TinyTest requires C11 or later")
    message(FATAL_ERROR
      "The pre-C11 probe failed for an unexpected reason:\n"
      "${tinytest_pre_c11_output}")
  endif()
endif()

set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_tinytest_saved_try_compile_target_type}")
unset(_tinytest_saved_try_compile_target_type)

cmake_add_test(tinytest_result_x_macro
  SOURCES result_x_macro_test.c
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

add_test(
  NAME tinytest_public_header_structure
  COMMAND ${CMAKE_COMMAND}
    "-DTINYTEST_HEADER=${CMAKE_SOURCE_DIR}/tinytest/include/tinytest.h"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/public_header_structure.cmake")

add_test(
  NAME tinymock_public_header_structure
  COMMAND ${CMAKE_COMMAND}
    "-DTINYMOCK_HEADER=${CMAKE_SOURCE_DIR}/tinytest/include/tinymock.h"
    "-DTRAITS_HEADER=${CMAKE_SOURCE_DIR}/tinytest/include/traits.h"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tinymock_header_structure.cmake")

cmake_add_test(tinymock_test
  SOURCES tinymock_test.c
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

cmake_add_executable(tinytest_focus_output
  SOURCES focus_output_cases.c
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

cmake_add_executable(tinytest_filter_output
  SOURCES filter_output_cases.c
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

cmake_add_executable(tinytest_junit_color
  SOURCES junit_color_cases.c
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

cmake_add_executable(tinytest_benchmark_invalid
  SOURCES benchmark_invalid_cases.c
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

cmake_add_executable(tinytest_benchmark_invalid_ops
  SOURCES benchmark_invalid_ops_cases.c
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

cmake_add_executable(tinytest_benchmark_invalid_bytes
  SOURCES benchmark_invalid_bytes_cases.c
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

cmake_add_executable(tinytest_benchmark_output
  SOURCES benchmark_output_cases.c
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

cmake_add_executable(tinytest_benchmark_output_plain
  SOURCES benchmark_output_cases.c
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

target_compile_definitions(tinytest_benchmark_output_plain
  PRIVATE BDD_BENCH_TABLE=0)

if(CMAKE_CXX_COMPILER OR CMAKE_CXX_COMPILER_ID)
  cmake_add_test(tinytest_regression_cpp
    SOURCES regression_cpp.cpp
    LIBS TurboUtils::TinyTest
    FOLDER "tinytest/tests")
  set_target_properties(tinytest_regression_cpp PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)

  cmake_add_test(tinymock_cpp_test
    SOURCES tinymock_cpp_test.cpp
    LIBS TurboUtils::TinyTest
    FOLDER "tinytest/tests")
  set_target_properties(tinymock_cpp_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)
endif()
