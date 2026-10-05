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

cmake_add_test(cmeta_coroutine_facade_test
  SOURCES cmeta_coroutine_facade_test.c
  LIBS Salts::Coroutine Salts::Platform Salts::TinyTest
  FOLDER "coroutine/tests")

cmake_add_test(cmeta_coroutine_header_cpp_test
  SOURCES cmeta_coroutine_header_cpp_test.cpp
  LIBS Salts::Coroutine
  FOLDER "coroutine/tests")


add_test(
  NAME coroutine_executor_private_batch_contract_test
  COMMAND ${CMAKE_COMMAND}
          -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/coroutine_executor_private_batch_contract_test.cmake)
