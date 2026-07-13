set(TARGET_NAME toml_parser)

cmake_add_grammar(TOML
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/parser/toml_lexer.re
  GRAMMAR_Y ${CMAKE_CURRENT_SOURCE_DIR}/parser/toml_grammar.y
  FOLDER "parser/${TARGET_NAME}/grammar")

# =============================================================================
# Library (respects BUILD_SHARED_LIBS)
# =============================================================================

cmake_add_source(SOURCE_FILES RECURSE DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
                 PATTERNS "*.c")

add_library(${TARGET_NAME} STATIC ${SOURCE_FILES} ${TOML_LEXER_GEN}
                                  ${TOML_GRAMMAR_GEN})
add_dependencies(${TARGET_NAME} ${TOML_LEXER_TARGET} ${TOML_GRAMMAR_TARGET})

cmake_config_target(${TARGET_NAME}
  ALIAS TurboUtils::TomlParser
  FOLDER "parser/${TARGET_NAME}"
  EXPORT_NAME TomlParser)
 
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src 
          ${CMAKE_CURRENT_SOURCE_DIR}/parser
          ${CMAKE_CURRENT_SOURCE_DIR}/..
          ${CMAKE_CURRENT_BINARY_DIR})
find_path(SIMDE_INCLUDE_DIRS NAMES simde/x86/sse2.h REQUIRED)
target_include_directories(${TARGET_NAME} PRIVATE ${SIMDE_INCLUDE_DIRS})

# =============================================================================
# Tools / Examples (Optional)
# =============================================================================

add_executable(toml2json tools/toml2json.c)
target_link_libraries(toml2json PRIVATE TurboUtils::TomlParser)
cmake_config_target(toml2json FOLDER "parser/${TARGET_NAME}/examples")

add_executable(toml-c-test tools/toml-c-test.c)
target_link_libraries(toml-c-test PRIVATE TurboUtils::TomlParser)
cmake_config_target(toml-c-test FOLDER "parser/${TARGET_NAME}/examples")

add_executable(test_toml tests/test_toml.c)
target_link_libraries(test_toml PRIVATE TurboUtils::TomlParser TurboUtils::TinyTest)
cmake_config_target(test_toml FOLDER "parser/${TARGET_NAME}/tests")
