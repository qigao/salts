cmake_minimum_required(VERSION 3.20)
project(SaltsCSTLInstallConsumer LANGUAGES C CXX)

find_package(Salts CONFIG REQUIRED)

add_executable(cstl_consumer_c consumer.c)
target_compile_features(cstl_consumer_c PRIVATE c_std_11)
target_link_libraries(cstl_consumer_c PRIVATE Salts::CSTL)

add_executable(cstl_consumer_cpp consumer.cpp)
target_compile_features(cstl_consumer_cpp PRIVATE cxx_std_17)
target_link_libraries(cstl_consumer_cpp PRIVATE Salts::CSTL)
