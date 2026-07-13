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

cmake_install_headers(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/)

# =============================================================================
# Tests
# =============================================================================

add_executable(test_tbe_parser test/test_tbe_parser.c)
target_link_libraries(test_tbe_parser PRIVATE TurboUtils::TbeSchema
                                              TurboUtils::TinyTest)
cmake_config_target(test_tbe_parser FOLDER "tbe/schema")
add_test(NAME test_tbe_parser COMMAND test_tbe_parser)

add_executable(test_flags_bdd test/test_flags_bdd.c)
target_link_libraries(test_flags_bdd PRIVATE TurboUtils::TbeSchema
                                             TurboUtils::TinyTest)
cmake_config_target(test_flags_bdd FOLDER "tbe/schema")
add_test(NAME test_flags_bdd COMMAND test_flags_bdd)

add_executable(test_tbe_parser_bdd test/test_tbe_parser_bdd.c)
target_link_libraries(test_tbe_parser_bdd PRIVATE TurboUtils::TbeSchema
                                                  TurboUtils::TinyTest)
cmake_config_target(test_tbe_parser_bdd FOLDER "tbe/schema")
add_test(NAME test_tbe_parser_bdd COMMAND test_tbe_parser_bdd)

add_executable(test_benchmark_schemas_bdd test/test_benchmark_schemas_bdd.c)
target_link_libraries(test_benchmark_schemas_bdd PRIVATE TurboUtils::TbeSchema
                                                         TurboUtils::TinyTest)
cmake_config_target(test_benchmark_schemas_bdd FOLDER "tbe/schema")
add_test(NAME test_benchmark_schemas_bdd COMMAND test_benchmark_schemas_bdd)

add_executable(test_tbe_robustness test/test_tbe_robustness.c)
target_link_libraries(test_tbe_robustness PRIVATE TurboUtils::TbeSchema
                                                  TurboUtils::TinyTest)
cmake_config_target(test_tbe_robustness FOLDER "tbe/schema")
add_test(NAME test_tbe_robustness COMMAND test_tbe_robustness)

add_executable(verify_fixes test/verify_fixes.c)
target_link_libraries(verify_fixes PRIVATE TurboUtils::TbeSchema)
cmake_config_target(verify_fixes FOLDER "tbe/schema")

add_executable(test_optional_and_defaults test/test_optional_and_defaults.c)
target_link_libraries(test_optional_and_defaults PRIVATE TurboUtils::TbeSchema
                                                         TurboUtils::TinyTest)
cmake_config_target(test_optional_and_defaults FOLDER "tbe/schema")
add_test(NAME test_optional_and_defaults COMMAND test_optional_and_defaults)

add_executable(test_enum_helpers test/test_enum_helpers.c)
target_link_libraries(test_enum_helpers PRIVATE TurboUtils::TbeSchema
                                                TurboUtils::TinyTest)
cmake_config_target(test_enum_helpers FOLDER "tbe/schema")
add_test(NAME test_enum_helpers COMMAND test_enum_helpers)

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
add_custom_target(generate_test_enum_enhanced_header
                  DEPENDS ${ENUM_ENHANCED_HEADER})

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
add_custom_target(generate_test_enum_enhanced_simple_header
                  DEPENDS ${ENUM_ENHANCED_SIMPLE_HEADER})

# 功能测试：枚举增强
add_executable(test_enum_enhanced_functional
               test/test_enum_enhanced_functional.c
               ${ENUM_ENHANCED_HEADER})
target_link_libraries(test_enum_enhanced_functional PRIVATE TurboUtils::TbeSchema
                                                            TurboUtils::TinyTest)
target_include_directories(test_enum_enhanced_functional PRIVATE ${CMAKE_BINARY_DIR}/bin)
add_dependencies(test_enum_enhanced_functional generate_test_enum_enhanced_header)
cmake_config_target(test_enum_enhanced_functional FOLDER "tbe/schema")
add_test(NAME test_enum_enhanced_functional COMMAND test_enum_enhanced_functional)

# 功能测试：枚举增强（简化版）
add_executable(test_enum_enhanced_simple_functional
               test/test_enum_enhanced_simple_functional.c
               ${ENUM_ENHANCED_SIMPLE_HEADER})
target_link_libraries(test_enum_enhanced_simple_functional PRIVATE TurboUtils::TbeSchema
                                                                    TurboUtils::TinyTest)
target_include_directories(test_enum_enhanced_simple_functional PRIVATE ${CMAKE_BINARY_DIR}/bin)
add_dependencies(test_enum_enhanced_simple_functional generate_test_enum_enhanced_simple_header)
cmake_config_target(test_enum_enhanced_simple_functional FOLDER "tbe/schema")
add_test(NAME test_enum_enhanced_simple_functional COMMAND test_enum_enhanced_simple_functional)

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
add_custom_target(generate_test_union_types_header
                  DEPENDS ${UNION_TYPES_HEADER})

# 功能测试：联合类型
add_executable(test_union_types_functional
               test/test_union_types_functional.c
               ${UNION_TYPES_HEADER})
target_link_libraries(test_union_types_functional PRIVATE TurboUtils::TbeSchema
                                                          TurboUtils::TinyTest)
target_include_directories(test_union_types_functional PRIVATE ${CMAKE_BINARY_DIR}/bin)
add_dependencies(test_union_types_functional generate_test_union_types_header)
cmake_config_target(test_union_types_functional FOLDER "tbe/schema")
add_test(NAME test_union_types_functional COMMAND test_union_types_functional)

# =============================================================================
# Benchmarks
# =============================================================================

add_executable(benchmark_tbe_simple test/benchmark_tbe_simple.c)
target_link_libraries(benchmark_tbe_simple PRIVATE TurboUtils::TbeSchema
                                                   TurboUtils::TinyTest)
cmake_config_target(benchmark_tbe_simple FOLDER "tbe/schema")

add_executable(benchmark_encoding test/benchmark_encoding.c)
target_link_libraries(benchmark_encoding PRIVATE TurboUtils::TinyTest)
cmake_config_target(benchmark_encoding FOLDER "tbe/schema")

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
