set(TARGET_NAME json_parser)

# Generated files
cmake_add_grammar(JSON
  LEXER_RE ${CMAKE_CURRENT_SOURCE_DIR}/parser/json_lexer.re
  GRAMMAR_Y ${CMAKE_CURRENT_SOURCE_DIR}/parser/json_grammar.y
  FOLDER "parser/${TARGET_NAME}/grammar")

cmake_add_grammar(JSONPATH
  GRAMMAR_Y ${CMAKE_CURRENT_SOURCE_DIR}/parser/jsonpath_grammar.y
  FOLDER "parser/${TARGET_NAME}/grammar")

# Library
cmake_add_source(SOURCE_FILES DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(${TARGET_NAME} STATIC ${SOURCE_FILES} ${JSON_LEXER_GEN}
                                  ${JSON_GRAMMAR_GEN} ${JSONPATH_GRAMMAR_GEN})
add_dependencies(${TARGET_NAME} ${JSON_LEXER_TARGET} ${JSON_GRAMMAR_TARGET}
                               ${JSONPATH_GRAMMAR_TARGET})

cmake_config_target(${TARGET_NAME}
  ALIAS TurboUtils::JsonParser
  FOLDER "parser/${TARGET_NAME}"
  EXPORT_NAME JsonParser)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include> $<INSTALL_INTERFACE:include/json_parser>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/parser ${CMAKE_CURRENT_BINARY_DIR})
find_path(SIMDE_INCLUDE_DIRS NAMES simde/x86/sse2.h REQUIRED)
target_include_directories(${TARGET_NAME} PRIVATE ${SIMDE_INCLUDE_DIRS})
target_link_libraries(${TARGET_NAME} PUBLIC TurboUtils::Core)

add_subdirectory(test)
if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
 
