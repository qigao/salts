project(tinytest)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(tinytest INTERFACE)
cmake_config_target(tinytest
  ALIAS TurboUtils::TinyTest
  FOLDER "tinytest"
  EXPORT_NAME TinyTest)

target_include_directories(
  tinytest INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                     $<INSTALL_INTERFACE:include>)

install(TARGETS tinytest EXPORT TurboUtilsTargets)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/tinytest.h DESTINATION include)

# Examples / self-tests
set(EXAMPLE_SOURCES example.c tinytest.h)
cmake_add_executable(example_test SOURCES ${EXAMPLE_SOURCES} FOLDER "tinytest")
set(EXAMPLE_SOURCES math_test.c tinytest.h)
cmake_add_executable(math_test SOURCES ${EXAMPLE_SOURCES} FOLDER "tinytest")
if(UNIX)
  target_link_libraries(math_test PRIVATE m)
endif()

set(TEST_TREE_SOURCES test-tree.c tinytest.h test-tree.h)
cmake_add_executable(test_tree_test SOURCES ${TEST_TREE_SOURCES} FOLDER "tinytest")

set(ARRAY_SOURCES array.c tinytest.h array.h)
cmake_add_executable(array_test SOURCES ${ARRAY_SOURCES} FOLDER "tinytest")

set(DYNAMIC_TEST_SOURCES dynamic-test.c tinytest.h array.h)
cmake_add_executable(dynamic_test SOURCES ${DYNAMIC_TEST_SOURCES} FOLDER "tinytest")

set(BEFORE_AFTER_SOURCES before-after.c tinytest.h)
cmake_add_executable(before_after SOURCES ${BEFORE_AFTER_SOURCES} FOLDER "tinytest")

set(GROUP_SUITE_SOURCES examples/group_suite_example.c tinytest.h)
cmake_add_executable(group_suite_example SOURCES ${GROUP_SUITE_SOURCES}
                     FOLDER "tinytest")

set(REGRESSION_RUNTIME_SOURCES regression_runtime.c tinytest.h)
set(REGRESSION_FIXES_SOURCES regression_fixes.c tinytest.h)
cmake_add_test(tinytest_regression_fixes
  SOURCES ${REGRESSION_FIXES_SOURCES}
  FOLDER "tinytest")

cmake_add_test(tinytest_regression_runtime
  SOURCES ${REGRESSION_RUNTIME_SOURCES}
  FOLDER "tinytest")

set(FOCUS_OUTPUT_SOURCES focus_output_cases.c tinytest.h)
cmake_add_executable(tinytest_focus_output SOURCES ${FOCUS_OUTPUT_SOURCES}
                     FOLDER "tinytest")

set(FILTER_OUTPUT_SOURCES filter_output_cases.c tinytest.h)
cmake_add_executable(tinytest_filter_output SOURCES ${FILTER_OUTPUT_SOURCES}
                     FOLDER "tinytest")

set(JUNIT_COLOR_SOURCES junit_color_cases.c tinytest.h)
cmake_add_executable(tinytest_junit_color SOURCES ${JUNIT_COLOR_SOURCES}
                     FOLDER "tinytest")

set(BENCHMARK_INVALID_SOURCES benchmark_invalid_cases.c tinytest.h)
cmake_add_executable(tinytest_benchmark_invalid
  SOURCES ${BENCHMARK_INVALID_SOURCES}
  FOLDER "tinytest")

set(BENCHMARK_INVALID_OPS_SOURCES benchmark_invalid_ops_cases.c tinytest.h)
cmake_add_executable(tinytest_benchmark_invalid_ops
  SOURCES ${BENCHMARK_INVALID_OPS_SOURCES}
  FOLDER "tinytest")

set(BENCHMARK_INVALID_BYTES_SOURCES benchmark_invalid_bytes_cases.c tinytest.h)
cmake_add_executable(tinytest_benchmark_invalid_bytes
  SOURCES ${BENCHMARK_INVALID_BYTES_SOURCES}
  FOLDER "tinytest")

set(BENCHMARK_OUTPUT_SOURCES benchmark_output_cases.c tinytest.h)
cmake_add_executable(tinytest_benchmark_output
  SOURCES ${BENCHMARK_OUTPUT_SOURCES}
  FOLDER "tinytest")
cmake_add_executable(tinytest_benchmark_output_plain
  SOURCES ${BENCHMARK_OUTPUT_SOURCES}
  FOLDER "tinytest")
target_compile_definitions(tinytest_benchmark_output_plain
  PRIVATE BDD_BENCH_TABLE=0)

add_test(
  NAME tinytest_focus_tap
  COMMAND ${CMAKE_COMMAND}
          -DTEST_EXE=$<TARGET_FILE:tinytest_focus_output>
          -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_focus_tap.cmake)

add_test(
  NAME tinytest_focus_console
  COMMAND ${CMAKE_COMMAND}
          -DTEST_EXE=$<TARGET_FILE:tinytest_focus_output>
          -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_focus_console.cmake)

add_test(
  NAME tinytest_filter_console
  COMMAND ${CMAKE_COMMAND}
          -DTEST_EXE=$<TARGET_FILE:tinytest_filter_output>
          -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_filter_console.cmake)

add_test(
  NAME tinytest_skip_console
  COMMAND ${CMAKE_COMMAND}
          -DTEST_EXE=$<TARGET_FILE:tinytest_filter_output>
          -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_skip_console.cmake)

add_test(
  NAME tinytest_filter_junit
  COMMAND ${CMAKE_COMMAND}
          -DTEST_EXE=$<TARGET_FILE:tinytest_filter_output>
          -DJUNIT_FILE=${CMAKE_CURRENT_BINARY_DIR}/tinytest-filter.junit.xml
          -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_filter_junit.cmake)

add_test(
  NAME tinytest_junit_color
  COMMAND ${CMAKE_COMMAND}
          -DTEST_EXE=$<TARGET_FILE:tinytest_junit_color>
          -DJUNIT_FILE=${CMAKE_CURRENT_BINARY_DIR}/tinytest-color.junit.xml
          -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_junit_color.cmake)

add_test(
  NAME tinytest_benchmark_invalid
  COMMAND ${CMAKE_COMMAND}
          -DTEST_EXE=$<TARGET_FILE:tinytest_benchmark_invalid>
          -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_benchmark_invalid.cmake)

add_test(
  NAME tinytest_benchmark_invalid_ops
  COMMAND ${CMAKE_COMMAND}
          -DTEST_EXE=$<TARGET_FILE:tinytest_benchmark_invalid_ops>
          "-DEXPECTED_DETAIL=requires at least one operation per sample"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_benchmark_invalid.cmake)

add_test(
  NAME tinytest_benchmark_invalid_bytes
  COMMAND ${CMAKE_COMMAND}
          -DTEST_EXE=$<TARGET_FILE:tinytest_benchmark_invalid_bytes>
          "-DEXPECTED_DETAIL=requires at least one byte per sample"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_benchmark_invalid.cmake)

add_test(
  NAME tinytest_benchmark_output
  COMMAND ${CMAKE_COMMAND}
          -DTEST_EXE=$<TARGET_FILE:tinytest_benchmark_output>
          -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_benchmark_output.cmake)

add_test(
  NAME tinytest_benchmark_output_plain
  COMMAND ${CMAKE_COMMAND}
          -DTEST_EXE=$<TARGET_FILE:tinytest_benchmark_output_plain>
          -DPLAIN_OUTPUT=ON
          -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_benchmark_output.cmake)

if(CMAKE_CXX_COMPILER OR CMAKE_CXX_COMPILER_ID)
  set(EXAMPLE_CPP_SOURCES example.cpp tinytest.h)
  cmake_add_executable(example_cpp_test SOURCES ${EXAMPLE_CPP_SOURCES}
                       FOLDER "tinytest")
  set_target_properties(example_cpp_test PROPERTIES CXX_STANDARD 17
                                                    CXX_STANDARD_REQUIRED ON)

  set(REGRESSION_CPP_SOURCES regression_cpp.cpp tinytest.h)
  cmake_add_test(tinytest_regression_cpp
    SOURCES ${REGRESSION_CPP_SOURCES}
    FOLDER "tinytest")
  set_target_properties(tinytest_regression_cpp PROPERTIES CXX_STANDARD 17
                                                           CXX_STANDARD_REQUIRED ON)
endif()

# JUnit to HTML Dashboard Tool
set(JUNIT_TO_HTML_SOURCES tools/junit_to_html.c)
cmake_add_executable(junit_to_html
  SOURCES ${JUNIT_TO_HTML_SOURCES}
  LIBS TurboUtils::Core TurboUtils::Mustache TurboUtils::JsonParser
       TurboUtils::CmdParser cxml
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
           ${CMAKE_SOURCE_DIR}/mustache/include
           ${CMAKE_SOURCE_DIR}/parser/internal/json_parser/include
           ${CMAKE_SOURCE_DIR}/parser/internal/cmd_parser/include
           ${CMAKE_SOURCE_DIR}/vendor/cxml/include
  FOLDER "tinytest")
