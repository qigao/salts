cmake_minimum_required(VERSION 3.20)
project(SaltsUnicodeInstalledConsumers LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Salts CONFIG REQUIRED)
if(NOT TARGET Salts::Unicode)
  message(FATAL_ERROR "Installed Salts SDK does not export Salts::Unicode")
endif()

add_executable(salts_unicode_installed_c11 main.c)
target_link_libraries(salts_unicode_installed_c11 PRIVATE Salts::Unicode)
add_executable(salts_unicode_installed_cpp17 main.cpp)
target_link_libraries(salts_unicode_installed_cpp17 PRIVATE Salts::Unicode)

enable_testing()
add_test(NAME salts_unicode_installed_c11 COMMAND salts_unicode_installed_c11)
add_test(NAME salts_unicode_installed_cpp17 COMMAND salts_unicode_installed_cpp17)
