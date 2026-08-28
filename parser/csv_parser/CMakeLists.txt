set(TARGET_NAME csv_parser)

cmake_add_grammar(CSV
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/parser/csv_lexer.re
  GRAMMAR_Y ${CMAKE_CURRENT_SOURCE_DIR}/parser/csv_grammar.y
  FOLDER "parser/${TARGET_NAME}/grammar")

cmake_add_grammar(DSV_FILTER_EXPR
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/parser/dsv_filter_expr_lexer.re
  GRAMMAR_Y ${CMAKE_CURRENT_SOURCE_DIR}/parser/dsv_filter_expr_grammar.y
  LEXER_DEPENDS_ON_GRAMMAR
  FOLDER "parser/${TARGET_NAME}/grammar")

# =============================================================================
# Library (respects BUILD_SHARED_LIBS)
# =============================================================================

cmake_add_source(SOURCE_FILES RECURSE DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
                 PATTERNS "*.c")

add_library(${TARGET_NAME} STATIC ${SOURCE_FILES} ${CSV_LEXER_GEN}
                                  ${CSV_GRAMMAR_GEN}
                                  ${DSV_FILTER_EXPR_LEXER_GEN}
                                  ${DSV_FILTER_EXPR_GRAMMAR_GEN})
add_dependencies(${TARGET_NAME} ${CSV_LEXER_TARGET} ${CSV_GRAMMAR_TARGET}
                 ${DSV_FILTER_EXPR_LEXER_TARGET}
                 ${DSV_FILTER_EXPR_GRAMMAR_TARGET})

cmake_config_target(${TARGET_NAME}
  ALIAS TurboUtils::CsvParser
  FOLDER "parser/${TARGET_NAME}"
  EXPORT_NAME CsvParser)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/parser ${CMAKE_CURRENT_BINARY_DIR})
find_path(SIMDE_INCLUDE_DIRS NAMES simde/x86/sse2.h REQUIRED)
target_include_directories(${TARGET_NAME} PRIVATE ${SIMDE_INCLUDE_DIRS})

target_link_libraries(${TARGET_NAME}
  PRIVATE TurboUtils::STL
  PUBLIC TurboUtils::Core)

target_link_libraries(${TARGET_NAME} PUBLIC TurboUtils::QueryVM)

# =============================================================================
# Tests
# =============================================================================

add_subdirectory(test)
if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
