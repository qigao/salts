cmake_add_test(platform_clock_test
  SOURCES platform_clock_test.c
  LIBS Rocida::Platform Rocida::TinyTest
  FOLDER "platform/tests")
set_target_properties(platform_clock_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(platform_random_test
  SOURCES platform_random_test.c
  LIBS Rocida::Platform Rocida::TinyTest
  FOLDER "platform/tests")
set_target_properties(platform_random_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(platform_thread_test
  SOURCES platform_thread_test.c
  LIBS Rocida::Platform Rocida::TinyTest
  FOLDER "platform/tests")
set_target_properties(platform_thread_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(platform_header_cpp_test
  SOURCES platform_header_cpp_test.cpp
  LIBS Rocida::Platform
  FOLDER "platform/tests")
set_target_properties(platform_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)

cmake_add_test(platform_readiness_fake_test
  SOURCES
    platform_readiness_fake_test.c
    readiness_backend_contract.c
    readiness_contract_suite.c
    readiness_fake_backend.c
  LIBS Rocida::Platform Rocida::TinyTest
  FOLDER "platform/tests")
set_target_properties(platform_readiness_fake_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(platform_readiness_state_model_test
  SOURCES platform_readiness_state_model_test.c
  LIBS Rocida::Platform Rocida::TinyTest
  FOLDER "platform/tests")
set_target_properties(platform_readiness_state_model_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

if(UNIX)
  cmake_add_test(platform_readiness_poll_test
    SOURCES
      platform_readiness_poll_test.c
      readiness_backend_contract.c
    LIBS Rocida::Platform Rocida::TinyTest
    FOLDER "platform/tests")
  set_target_properties(platform_readiness_poll_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   TURBO_ENABLE_EPOLL_READINESS)
  cmake_add_test(platform_readiness_epoll_test
    SOURCES
      platform_readiness_epoll_test.c
      readiness_backend_contract.c
    LIBS Rocida::Platform Rocida::TinyTest
    FOLDER "platform/tests")
  set_target_properties(platform_readiness_epoll_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
elseif(APPLE)
  cmake_add_test(platform_readiness_kqueue_test
    SOURCES platform_readiness_kqueue_test.c
    LIBS Rocida::Platform Rocida::TinyTest
    FOLDER "platform/tests")
  set_target_properties(platform_readiness_kqueue_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
else()
  cmake_add_test(platform_readiness_unsupported_test
    SOURCES platform_readiness_unsupported_test.c
    LIBS Rocida::Platform Rocida::TinyTest
    FOLDER "platform/tests")
  set_target_properties(platform_readiness_unsupported_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
  if(NOT UNIX)
    target_compile_definitions(platform_readiness_unsupported_test PRIVATE
      TURBO_TEST_EXPECT_POLL_UNSUPPORTED=1)
  endif()
endif()
