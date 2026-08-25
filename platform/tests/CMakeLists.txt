cmake_add_test(platform_clock_test
  SOURCES platform_clock_test.c
  LIBS TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "platform/tests")
set_target_properties(platform_clock_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(platform_thread_test
  SOURCES platform_thread_test.c
  LIBS TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "platform/tests")
set_target_properties(platform_thread_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(platform_header_cpp_test
  SOURCES platform_header_cpp_test.cpp
  LIBS TurboUtils::Platform
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
  LIBS TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "platform/tests")
set_target_properties(platform_readiness_fake_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(platform_readiness_state_model_test
  SOURCES platform_readiness_state_model_test.c
  LIBS TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "platform/tests")
set_target_properties(platform_readiness_state_model_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   TURBO_ENABLE_EPOLL_READINESS)
  cmake_add_test(platform_readiness_epoll_test
    SOURCES
      platform_readiness_epoll_test.c
      readiness_backend_contract.c
    LIBS TurboUtils::Platform TurboUtils::TinyTest
    FOLDER "platform/tests")
  set_target_properties(platform_readiness_epoll_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
elseif(APPLE)
  cmake_add_test(platform_readiness_kqueue_test
    SOURCES platform_readiness_kqueue_test.c
    LIBS TurboUtils::Platform TurboUtils::TinyTest
    FOLDER "platform/tests")
  set_target_properties(platform_readiness_kqueue_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
else()
  cmake_add_test(platform_readiness_unsupported_test
    SOURCES platform_readiness_unsupported_test.c
    LIBS TurboUtils::Platform TurboUtils::TinyTest
    FOLDER "platform/tests")
  set_target_properties(platform_readiness_unsupported_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
endif()
