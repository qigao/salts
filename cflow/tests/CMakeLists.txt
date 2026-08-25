set(CFLOW_TEST_SUPPORT
  cflow_test_ops.c
  cflow_test_ops.h)

cmake_add_test(cflow_graph_test
  SOURCES
    cflow_graph_test.c
    ${CFLOW_TEST_SUPPORT}
  LIBS TurboUtils::CFlow TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_dense_successor_index_test
  SOURCES cflow_dense_successor_index_test.c
  LIBS TurboUtils::CFlow TurboUtils::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
  FOLDER "cflow/tests")

cmake_add_test(cflow_branching_views_test
  SOURCES
    cflow_branching_views_test.c
    ../benchmarks/cflow_branching_views.c
  LIBS TurboUtils::CFlow TurboUtils::STL TurboUtils::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../benchmarks
  FOLDER "cflow/tests")

cmake_add_test(cflow_pipeline_test
  SOURCES
    cflow_pipeline_test.c
    ${CFLOW_TEST_SUPPORT}
  LIBS TurboUtils::CFlow TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_runtime_test
  SOURCES
    cflow_runtime_test.c
    ${CFLOW_TEST_SUPPORT}
  LIBS TurboUtils::CFlow TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_readiness_test
  SOURCES
    cflow_readiness_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../platform/tests/readiness_fake_backend.c
  LIBS TurboUtils::CFlow TurboUtils::Platform TurboUtils::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../../platform/tests
  FOLDER "cflow/tests")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   TURBO_ENABLE_EPOLL_READINESS)
  target_compile_definitions(cflow_readiness_test PRIVATE
    CFLOW_TEST_EPOLL_READINESS=1)
endif()

cmake_add_test(cflow_event_mailbox_test
  SOURCES cflow_event_mailbox_test.c
  LIBS TurboUtils::CFlow TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_machine_test
  SOURCES cflow_machine_test.c
  LIBS TurboUtils::CFlow TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_machine_hierarchy_test
  SOURCES cflow_machine_hierarchy_test.c
  LIBS TurboUtils::CFlow TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_machine_runtime_test
  SOURCES cflow_machine_runtime_test.c
  LIBS TurboUtils::CFlow TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_actor_test
  SOURCES cflow_actor_test.c
  LIBS TurboUtils::CFlow TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_io_actor_test
  SOURCES cflow_io_actor_test.c
  LIBS TurboUtils::CFlow TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_io_native_test
  SOURCES cflow_io_native_test.c
  LIBS TurboUtils::CFlow TurboUtils::Platform TurboUtils::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
  FOLDER "cflow/tests")
if(WIN32)
  target_link_libraries(cflow_io_native_test PRIVATE ws2_32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
       TURBO_ENABLE_EPOLL_READINESS)
  target_compile_definitions(cflow_io_native_test PRIVATE
    CFLOW_TEST_NATIVE_EPOLL=1)
endif()

cmake_add_test(cflow_timer_event_test
  SOURCES cflow_timer_event_test.c
  LIBS TurboUtils::CFlow TurboUtils::Platform TurboUtils::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
  FOLDER "cflow/tests")

cmake_add_test(cflow_temporal_test
  SOURCES cflow_temporal_test.c
  LIBS TurboUtils::CFlow TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_calculus_conformance_test
  SOURCES
    cflow_calculus_conformance_test.c
    ${CFLOW_TEST_SUPPORT}
  LIBS TurboUtils::CFlow TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_direct_test
  SOURCES cflow_direct_test.c
  LIBS TurboUtils::CFlow TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_parallel_reduce_test
  SOURCES cflow_parallel_reduce_test.c
  LIBS TurboUtils::CFlow TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_certificate_test
  SOURCES cflow_certificate_test.c
  LIBS TurboUtils::CFlow TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_execution_test
  SOURCES cflow_execution_test.c
  LIBS TurboUtils::CFlow TurboUtils::Platform TurboUtils::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../src
  FOLDER "cflow/tests")

cmake_add_test(cflow_scheduler_compat_test
  SOURCES cflow_scheduler_compat_test.c
  LIBS TurboUtils::CFlow TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_header_cpp_test
  SOURCES cflow_header_cpp_test.cpp
  LIBS TurboUtils::CFlow TurboUtils::TinyTest
  FOLDER "cflow/tests")

cmake_add_test(cflow_operator_policy_test
  SOURCES cflow_operator_policy_test.c
  LIBS TurboUtils::CFlow TurboUtils::TinyTest
  FOLDER "cflow/tests")

set_target_properties(
  cflow_graph_test
  cflow_dense_successor_index_test
  cflow_branching_views_test
  cflow_pipeline_test
  cflow_runtime_test
  cflow_readiness_test
  cflow_event_mailbox_test
  cflow_machine_test
  cflow_machine_hierarchy_test
  cflow_machine_runtime_test
  cflow_actor_test
  cflow_io_actor_test
  cflow_io_native_test
  cflow_timer_event_test
  cflow_temporal_test
  cflow_calculus_conformance_test
  cflow_direct_test
  cflow_parallel_reduce_test
  cflow_certificate_test
  cflow_execution_test
  cflow_scheduler_compat_test
  cflow_operator_policy_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

set_target_properties(cflow_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
