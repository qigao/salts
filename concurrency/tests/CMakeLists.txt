cmake_add_test(disruptor_test
  SOURCES disruptor_test.c
  LIBS Salts::Concurrency Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
  FOLDER "concurrency/tests")
set_target_properties(disruptor_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(thread_pool_test
  SOURCES thread_pool_test.c
  LIBS Salts::Concurrency Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
  FOLDER "concurrency/tests")
set_target_properties(thread_pool_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(concurrency_deadline_queue_test
  SOURCES deadline_queue_test.c
  LIBS Salts::Concurrency Salts::TinyTest
  FOLDER "concurrency/tests")
set_target_properties(concurrency_deadline_queue_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(concurrency_header_cpp_test
  SOURCES concurrency_header_cpp_test.cpp
  LIBS Salts::Concurrency
  FOLDER "concurrency/tests")
set_target_properties(concurrency_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)

cmake_add_test(concurrency_rcu_test
  SOURCES rcu_test.c
  LIBS Salts::Concurrency Salts::TinyTest
  FOLDER "concurrency/tests")
set_target_properties(concurrency_rcu_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_tests_properties(concurrency_rcu_test PROPERTIES TIMEOUT 60)

cmake_add_test(concurrency_typed_primitives_test
  SOURCES typed_primitives_test.c
  LIBS Salts::Concurrency Salts::TinyTest
  FOLDER "concurrency/tests")
set_target_properties(concurrency_typed_primitives_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
cmake_add_test(concurrency_typed_rcu_cpp_test
  SOURCES typed_rcu_cpp_test.cpp
  LIBS Salts::Concurrency Salts::TinyTest
  FOLDER "concurrency/tests")
set_target_properties(concurrency_typed_rcu_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(concurrency_typed_rcu_cpp_test PRIVATE -Werror=old-style-cast)
endif()
set_tests_properties(concurrency_typed_primitives_test concurrency_typed_rcu_cpp_test
  PROPERTIES TIMEOUT 60)
