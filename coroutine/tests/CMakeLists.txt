cmake_add_test(salts_coro_executor_test
  SOURCES salts_coro_executor_test.c
  LIBS Salts::Coroutine Salts::Platform Salts::TinyTest
  FOLDER "coroutine/tests")

cmake_add_test(salts_coro_executor_header_cpp_test
  SOURCES salts_coro_executor_header_cpp_test.cpp
  LIBS Salts::Coroutine
  FOLDER "coroutine/tests")
