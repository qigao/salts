cmake_minimum_required(VERSION 3.20)
project(TurboUtilsContainerInstallConsumer LANGUAGES C CXX)

find_package(TurboUtils CONFIG REQUIRED)

add_executable(container_consumer_c consumer.c)
target_compile_features(container_consumer_c PRIVATE c_std_11)
target_link_libraries(container_consumer_c PRIVATE TurboUtils::Container)

add_executable(container_consumer_cpp consumer.cpp)
target_compile_features(container_consumer_cpp PRIVATE cxx_std_17)
target_link_libraries(container_consumer_cpp PRIVATE TurboUtils::Container)
