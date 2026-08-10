# JSON Parser tests
set(TEST_SOURCES test_json_parser.c)

cmake_add_test(test_json_parser
    SOURCES ${TEST_SOURCES}
    FOLDER "parser/${TARGET_NAME}/test"
    LIBS json_parser tinytest
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

cmake_add_test(test_json_parser_re
    SOURCES test_re.c
    FOLDER "parser/${TARGET_NAME}/test"
    LIBS json_parser tinytest
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include
             ${CMAKE_CURRENT_SOURCE_DIR}/../parser
             ${PROJECT_SOURCE_DIR}/utils/src
             ${SIMDE_INCLUDE_DIRS}
)
