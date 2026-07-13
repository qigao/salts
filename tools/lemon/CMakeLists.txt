add_executable(lemon lemon.c)
cmake_config_target(lemon ALIAS TurboUtils::Lemon FOLDER "tools")
target_compile_features(lemon PRIVATE c_std_11)

