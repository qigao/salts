# TinyTest self-tests and output/diagnostic executables.
#
# The framework headers remain in the parent directory and are provided through
# TurboUtils::TinyTest, so each target only lists the test source it owns.

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

cmake_add_test(tinytest_result_x_macro
  SOURCES result_x_macro_test.c
  LIBS TurboUtils::TinyTest
  FOLDER "tinytest/tests")

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
endif()
