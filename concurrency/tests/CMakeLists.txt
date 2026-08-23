cmake_add_test(disruptor_test
  SOURCES disruptor_test.c
  LIBS TurboUtils::Concurrency TurboUtils::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
  FOLDER "concurrency/tests")
set_target_properties(disruptor_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(thread_pool_test
  SOURCES thread_pool_test.c
  LIBS TurboUtils::Concurrency TurboUtils::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
  FOLDER "concurrency/tests")
set_target_properties(thread_pool_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(concurrency_header_cpp_test
  SOURCES concurrency_header_cpp_test.cpp
  LIBS TurboUtils::Concurrency
  FOLDER "concurrency/tests")
set_target_properties(concurrency_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
