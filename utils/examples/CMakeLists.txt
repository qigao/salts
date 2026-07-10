# Common module tests C++ Integration Test
add_executable(cpp_test_tlog cpp_test.cpp)
target_link_libraries(cpp_test_tlog PRIVATE TurboUtils::TinyTest TurboUtils::Core)
cmake_config_target(cpp_test_tlog NO_INSTALL FOLDER "utils/exmaples")

add_executable(test_spec test_spec.cpp)
target_link_libraries(test_spec PRIVATE TurboUtils::TinyTest TurboUtils::Core)
cmake_config_target(test_spec NO_INSTALL FOLDER "utils/exmaples")

# FS example
add_executable(fs_example fs_example.c)
target_link_libraries(fs_example PRIVATE TurboUtils::TinyTest TurboUtils::Core)
cmake_config_target(fs_example NO_INSTALL FOLDER "utils/exmaples")
