# Log pattern lexer using re2c

cmake_add_grammar(LOG_PATTERN
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/log_pattern_lexer.re
  LEXER_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/log_pattern_lexer.h
  FOLDER "utils/parser")

# Create parser library
add_library(log_pattern_lexer STATIC ${LOG_PATTERN_LEXER_GEN})
add_dependencies(log_pattern_lexer ${LOG_PATTERN_LEXER_TARGET})

cmake_config_target(log_pattern_lexer
  NO_INSTALL
  FOLDER "utils/parser"
  OUTPUT_NAME "log_pattern_lexer")

target_include_directories(
  log_pattern_lexer PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
find_path(SIMDE_INCLUDE_DIRS "simde/x86/sse2.h" REQUIRED)
target_include_directories(log_pattern_lexer PRIVATE ${PROJECT_SOURCE_DIR}/parser ${SIMDE_INCLUDE_DIRS})

# Export for parent
set(LOG_PATTERN_LEXER_LIBRARY
    log_pattern_lexer
    PARENT_SCOPE)
set(LOG_PATTERN_LEXER_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
    PARENT_SCOPE)

# ============================================================================
# fmt lexer using re2c
# ============================================================================

cmake_add_grammar(FMT
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/fmt_lexer.re
  LEXER_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fmt_lexer.h
  FOLDER "utils/parser")

# Create fmt lexer library
add_library(fmt_lexer STATIC ${FMT_LEXER_GEN})
add_dependencies(fmt_lexer ${FMT_LEXER_TARGET})

cmake_config_target(fmt_lexer
  NO_INSTALL
  FOLDER "utils/parser"
  OUTPUT_NAME "fmt_lexer")

target_include_directories(
  fmt_lexer PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                   $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
target_include_directories(fmt_lexer PRIVATE ${PROJECT_SOURCE_DIR}/parser ${SIMDE_INCLUDE_DIRS})

# Export for parent
set(FMT_LEXER_LIBRARY
    fmt_lexer
    PARENT_SCOPE)
set(FMT_LEXER_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
    PARENT_SCOPE)
