set(TEST_NAME test_dotenv)

add_executable(${TEST_NAME}
    test_dotenv.c
)

target_link_libraries(${TEST_NAME}
    PRIVATE
        TurboUtils::DotEnvParser
        TurboUtils::TinyTest
)

target_include_directories(${TEST_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

cmake_config_target(${TEST_NAME} NO_INSTALL FOLDER "parser/dotenv_parser/test")
set_target_properties(${TEST_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
