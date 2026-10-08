cmake_add_test(cflow_io_cnet_adapter_test
  SOURCES cflow_io_cnet_adapter_test.c
  LIBS Salts::CFlowCNet Salts::Platform Salts::TinyTest
  FOLDER "cflow/cnet-adapter/tests")
if(WIN32)
  target_link_libraries(cflow_io_cnet_adapter_test PRIVATE ws2_32)
endif()

cmake_add_test(cflow_cnet_domain_actor_test
  SOURCES cflow_cnet_domain_actor_test.c
  LIBS Salts::CFlowCNet Salts::Platform Salts::TinyTest
  FOLDER "cflow/cnet-adapter/tests")

cmake_add_test(cflow_cnet_domain_route_test
  SOURCES cflow_cnet_domain_route_test.c
  LIBS Salts::CFlowCNet Salts::Platform Salts::TinyTest
  FOLDER "cflow/cnet-adapter/tests")
if(WIN32)
  target_link_libraries(cflow_cnet_domain_route_test PRIVATE ws2_32)
endif()

cmake_add_test(cflow_cnet_manager_context_test
  SOURCES cflow_cnet_manager_context_test.c
  LIBS Salts::CFlowCNetManager Salts::Platform Salts::TinyTest
  FOLDER "cflow/cnet-adapter/tests")

cmake_add_test(cflow_cnet_header_cpp_test
  SOURCES cflow_cnet_header_cpp_test.cpp
  LIBS Salts::CFlowCNet Salts::TinyTest
  FOLDER "cflow/cnet-adapter/tests")

set_target_properties(
  cflow_io_cnet_adapter_test
  cflow_cnet_domain_actor_test
  cflow_cnet_domain_route_test
  cflow_cnet_manager_context_test
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

set_target_properties(
  cflow_cnet_header_cpp_test
  PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

