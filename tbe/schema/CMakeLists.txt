set(TARGET_NAME tbe_schema)

cmake_add_grammar(SCHEMA
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/parser/schema_lexer.re
  GRAMMAR_Y ${CMAKE_CURRENT_SOURCE_DIR}/parser/schema_grammar.y
  FOLDER "tbe/${TARGET_NAME}/grammar")

# =============================================================================
# Library
# =============================================================================

cmake_add_source(SOURCE_FILES RECURSE
  DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include
  PATTERNS "*.c*" "*.h*")

add_library(
  ${TARGET_NAME} STATIC
  ${SOURCE_FILES} ${SCHEMA_LEXER_GEN} ${SCHEMA_GRAMMAR_GEN} ${SCHEMA_GRAMMAR_Y}
  ${SCHEMA_LEXER_RE})
add_dependencies(${TARGET_NAME} ${SCHEMA_LEXER_TARGET} ${SCHEMA_GRAMMAR_TARGET})

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/parser ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(${TARGET_NAME} PUBLIC TurboUtils::Core)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS TurboUtils::TbeSchema
  FOLDER tbe/schema
  EXPORT_NAME TbeSchema)

if(NOT TARGET TurboUtils::SchemaBE)
  add_library(TurboUtils::SchemaBE ALIAS ${TARGET_NAME})
endif()

install(
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
  DESTINATION include
  FILES_MATCHING
  PATTERN "*.h")

# =============================================================================
# Tests
# =============================================================================

set(TBE_SCHEMA_TEST_LIBS TurboUtils::TbeSchema TurboUtils::TinyTest)
foreach(test_name
        test_tbe_parser
        test_flags_bdd
        test_tbe_parser_bdd
        test_benchmark_schemas_bdd
        test_tbe_robustness
        test_optional_and_defaults
        test_enum_helpers)
  cmake_add_test(${test_name}
    SOURCES test/${test_name}.c
    LIBS ${TBE_SCHEMA_TEST_LIBS}
    FOLDER "tbe/schema/tests")
endforeach()

cmake_add_executable(verify_fixes
  SOURCES test/verify_fixes.c
  LIBS TurboUtils::TbeSchema
  FOLDER "tbe/schema/tests")

# =============================================================================
# 枚举增强功能测试：生成头文件
# =============================================================================

set(C_STRUCT_TEMPLATE ${CMAKE_SOURCE_DIR}/tbe/tbe_compiler/templates/c_structs.mustache)

set(ENUM_ENHANCED_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/test/test_enum_enhanced.schema)
set(ENUM_ENHANCED_HEADER ${CMAKE_BINARY_DIR}/bin/test_enum_enhanced.h)
add_custom_command(
  OUTPUT ${ENUM_ENHANCED_HEADER}
  COMMAND tbe_compiler ${ENUM_ENHANCED_SCHEMA}
          --template ${C_STRUCT_TEMPLATE} --lang c --output ${ENUM_ENHANCED_HEADER}
  WORKING_DIRECTORY $<TARGET_FILE_DIR:tbe_compiler>
  DEPENDS tbe_compiler ${ENUM_ENHANCED_SCHEMA} ${C_STRUCT_TEMPLATE}
  COMMENT "Generating test_enum_enhanced.h"
  VERBATIM)

set(ENUM_ENHANCED_SIMPLE_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/test/test_enum_enhanced_simple.schema)
set(ENUM_ENHANCED_SIMPLE_HEADER ${CMAKE_BINARY_DIR}/bin/test_enum_enhanced_simple.h)
add_custom_command(
  OUTPUT ${ENUM_ENHANCED_SIMPLE_HEADER}
  COMMAND tbe_compiler ${ENUM_ENHANCED_SIMPLE_SCHEMA}
          --template ${C_STRUCT_TEMPLATE} --lang c --output ${ENUM_ENHANCED_SIMPLE_HEADER}
  WORKING_DIRECTORY $<TARGET_FILE_DIR:tbe_compiler>
  DEPENDS tbe_compiler ${ENUM_ENHANCED_SIMPLE_SCHEMA} ${C_STRUCT_TEMPLATE}
  COMMENT "Generating test_enum_enhanced_simple.h"
  VERBATIM)

# 功能测试：枚举增强
cmake_add_test(test_enum_enhanced_functional
  SOURCES test/test_enum_enhanced_functional.c ${ENUM_ENHANCED_HEADER}
  LIBS ${TBE_SCHEMA_TEST_LIBS}
  INCLUDES ${CMAKE_BINARY_DIR}/bin
  FOLDER "tbe/schema/tests")

# 功能测试：枚举增强（简化版）
cmake_add_test(test_enum_enhanced_simple_functional
  SOURCES test/test_enum_enhanced_simple_functional.c ${ENUM_ENHANCED_SIMPLE_HEADER}
  LIBS ${TBE_SCHEMA_TEST_LIBS}
  INCLUDES ${CMAKE_BINARY_DIR}/bin
  FOLDER "tbe/schema/tests")

set(UNION_TYPES_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/test/test_union_types.schema)
set(UNION_TYPES_HEADER ${CMAKE_BINARY_DIR}/bin/test_union_types.h)
add_custom_command(
  OUTPUT ${UNION_TYPES_HEADER}
  COMMAND tbe_compiler ${UNION_TYPES_SCHEMA}
          --template ${C_STRUCT_TEMPLATE} --lang c --output ${UNION_TYPES_HEADER}
  WORKING_DIRECTORY $<TARGET_FILE_DIR:tbe_compiler>
  DEPENDS tbe_compiler ${UNION_TYPES_SCHEMA} ${C_STRUCT_TEMPLATE}
  COMMENT "Generating test_union_types.h"
  VERBATIM)

# 功能测试：联合类型
cmake_add_test(test_union_types_functional
  SOURCES test/test_union_types_functional.c ${UNION_TYPES_HEADER}
  LIBS ${TBE_SCHEMA_TEST_LIBS}
  INCLUDES ${CMAKE_BINARY_DIR}/bin
  FOLDER "tbe/schema/tests")

# =============================================================================
# Benchmarks
# =============================================================================

cmake_add_benchmark(benchmark_tbe_simple
  SOURCES test/benchmark_tbe_simple.c
  LIBS ${TBE_SCHEMA_TEST_LIBS}
  FOLDER "tbe/schema/benchmarks")

cmake_add_benchmark(benchmark_encoding
  SOURCES test/benchmark_encoding.c
  LIBS TurboUtils::TinyTest
  FOLDER "tbe/schema/benchmarks")

# =============================================================================
# TBE vs Native Struct Benchmark
# =============================================================================

set(BENCH_SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/test/bench.schema)
set(BENCH_HEADER ${CMAKE_CURRENT_BINARY_DIR}/bench_data.h)
set(C_STRUCT_TEMPLATE
    ${CMAKE_SOURCE_DIR}/tbe/tbe_compiler/templates/c_structs.mustache)

add_custom_command(
  OUTPUT ${BENCH_HEADER}
  COMMAND tbe_compiler ${BENCH_SCHEMA} --template ${C_STRUCT_TEMPLATE}
          --lang c --output ${BENCH_HEADER}
  DEPENDS tbe_compiler ${BENCH_SCHEMA} ${C_STRUCT_TEMPLATE}
  COMMENT "Generating benchmark headers with tbe_compiler"
  VERBATIM)
