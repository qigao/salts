cmake_add_test(cflow_io_cnet_adapter_test
  SOURCES cflow_io_cnet_adapter_test.c
  LIBS Salts::CFlowCNet Salts::Platform Salts::TinyTest
  FOLDER "cflow/cnet-adapter/tests")
if(WIN32)
  target_link_libraries(cflow_io_cnet_adapter_test PRIVATE ws2_32)
endif()

cmake_add_test(cflow_cnet_header_cpp_test
  SOURCES cflow_cnet_header_cpp_test.cpp
  LIBS Salts::CFlowCNet Salts::TinyTest
  FOLDER "cflow/cnet-adapter/tests")

set_target_properties(
  cflow_io_cnet_adapter_test
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

add_test(
  NAME cflow_cnet_dependency_contract_test
  COMMAND ${CMAKE_COMMAND}
          -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cflow_cnet_dependency_contract_test.cmake)
