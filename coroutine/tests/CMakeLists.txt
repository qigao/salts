cmake_add_test(salts_coro_executor_test
  SOURCES salts_coro_executor_test.c
  LIBS Salts::Coroutine Salts::Platform Salts::TinyTest
  FOLDER "coroutine/tests")

target_include_directories(salts_coro_executor_test PRIVATE
  ${PROJECT_SOURCE_DIR}/coroutine/src)

cmake_add_test(salts_coro_executor_header_cpp_test
  SOURCES salts_coro_executor_header_cpp_test.cpp
  LIBS Salts::Coroutine
  FOLDER "coroutine/tests")


