cmake_add_test(coro_executor_test
  SOURCES coro_executor_test.c
  LIBS Salts::Coroutine Salts::Platform Salts::TinyTest
  FOLDER "coroutine/tests")

target_include_directories(coro_executor_test PRIVATE
  ${PROJECT_SOURCE_DIR}/coroutine/src)

cmake_add_test(coro_executor_header_cpp_test
  SOURCES coro_executor_header_cpp_test.cpp
  LIBS Salts::Coroutine
  FOLDER "coroutine/tests")

cmake_add_test(cmeta_coroutine_facade_test
  SOURCES cmeta_coroutine_facade_test.c
  LIBS Salts::Coroutine Salts::Platform Salts::TinyTest
  FOLDER "coroutine/tests")

cmake_add_test(cmeta_coroutine_header_cpp_test
  SOURCES cmeta_coroutine_header_cpp_test.cpp
  LIBS Salts::Coroutine
  FOLDER "coroutine/tests")

set_tests_properties(cmeta_coroutine_facade_test PROPERTIES TIMEOUT 60)
