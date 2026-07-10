set(TARGET_NAME test_datetime_parser)

add_executable(${TARGET_NAME} test_datetime_parser.cpp)

target_link_libraries(${TARGET_NAME} PRIVATE TurboUtils::DateTimeParser TurboUtils::TinyTest)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})

cmake_config_target(${TARGET_NAME} NO_INSTALL FOLDER "parser/datetime_parser/test")
