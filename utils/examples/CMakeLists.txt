# Common module tests C++ Integration Test
cmake_add_executable(cpp_test_tlog
  SOURCES cpp_test.cpp
  LIBS TurboUtils::TinyTest TurboUtils::Core
  FOLDER "utils/examples")

cmake_add_executable(test_spec
  SOURCES test_spec.cpp
  LIBS TurboUtils::TinyTest TurboUtils::Core
  FOLDER "utils/examples")

# FS example
cmake_add_executable(fs_example
  SOURCES fs_example.c
  LIBS TurboUtils::TinyTest TurboUtils::Core
  FOLDER "utils/examples")
