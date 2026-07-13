cmake_add_executable(lemon SOURCES lemon.c FOLDER "tools")
add_executable(TurboUtils::Lemon ALIAS lemon)
target_compile_features(lemon PRIVATE c_std_11)

