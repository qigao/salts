cmake_minimum_required(VERSION 3.20)
project(TinyMockInstalledSdk LANGUAGES C)

include(CTest)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Salts CONFIG REQUIRED)

add_executable(tinymock_installed_sdk_test consumer.c)
target_link_libraries(tinymock_installed_sdk_test PRIVATE Salts::TinyMock)

tinymock_add_function_mocks(tinymock_installed_sdk_test
  HEADERS reflected_api.h)

add_test(NAME tinymock_installed_sdk_test COMMAND tinymock_installed_sdk_test)
