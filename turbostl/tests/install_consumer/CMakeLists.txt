cmake_minimum_required(VERSION 3.20)
project(RocidaSTLInstallConsumer LANGUAGES C CXX)

find_package(Rocida CONFIG REQUIRED)

add_executable(turbostl_consumer_c consumer.c)
target_compile_features(turbostl_consumer_c PRIVATE c_std_11)
target_link_libraries(turbostl_consumer_c PRIVATE Rocida::STL)

add_executable(turbostl_consumer_cpp consumer.cpp)
target_compile_features(turbostl_consumer_cpp PRIVATE cxx_std_17)
target_link_libraries(turbostl_consumer_cpp PRIVATE Rocida::STL)
