# TinyTest examples.
#
# Examples are regular executables rather than CTest cases. They link
# Salts::TinyTest, which also supplies the parent tinytest include path.

cmake_add_executable(example_test
  SOURCES example.c
  LIBS Salts::TinyTest
  FOLDER "tinytest/examples")

cmake_add_executable(group_suite_example
  SOURCES group_suite_example.c
  LIBS Salts::TinyTest
  FOLDER "tinytest/examples")

if(CMAKE_CXX_COMPILER OR CMAKE_CXX_COMPILER_ID)
  cmake_add_executable(example_cpp_test
    SOURCES example.cpp
    LIBS Salts::TinyTest
    FOLDER "tinytest/examples")
  set_target_properties(example_cpp_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)
endif()
