cmake_minimum_required(VERSION 3.20)
project(SaltsCppObjectFacadeConsumer LANGUAGES CXX)

find_package(Salts CONFIG REQUIRED)

add_executable(salts_cpp_object_facade_consumer main.cpp)
target_compile_features(salts_cpp_object_facade_consumer PRIVATE cxx_std_17)
target_link_libraries(salts_cpp_object_facade_consumer
  PRIVATE
    Salts::BindingsCpp
    Salts::Lua
    Salts::QuickJS)
