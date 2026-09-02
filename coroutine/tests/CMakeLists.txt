cmake_add_test(turbo_coro_executor_test
  SOURCES turbo_coro_executor_test.c
  LIBS Rocida::Coroutine Rocida::Platform Rocida::TinyTest
  FOLDER "coroutine/tests")

cmake_add_test(turbo_coro_executor_header_cpp_test
  SOURCES turbo_coro_executor_header_cpp_test.cpp
  LIBS Rocida::Coroutine
  FOLDER "coroutine/tests")
