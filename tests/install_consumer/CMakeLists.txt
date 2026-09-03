cmake_minimum_required(VERSION 3.20)
project(RocidaInstallConsumer LANGUAGES C CXX)

find_package(Rocida CONFIG REQUIRED)

function(add_rocida_consumer name target contract)
  add_executable(${name} consumer.c)
  target_compile_features(${name} PRIVATE c_std_11)
  target_compile_definitions(${name} PRIVATE ${contract}=1)
  target_link_libraries(${name} PRIVATE ${target})
endfunction()

add_rocida_consumer(consume_platform Rocida::Platform CONSUME_PLATFORM)
add_rocida_consumer(consume_concurrency Rocida::Concurrency
                       CONSUME_CONCURRENCY)
add_rocida_consumer(consume_coroutine Rocida::Coroutine
                       CONSUME_COROUTINE)
add_rocida_consumer(consume_native_io Rocida::NativeIO
                       CONSUME_NATIVE_IO)
add_rocida_consumer(consume_cnet Rocida::CNet CONSUME_CNET)
add_rocida_consumer(consume_chttp Rocida::CHTTP CONSUME_CHTTP)
add_rocida_consumer(consume_crpc Rocida::CRPC CONSUME_CRPC)
add_rocida_consumer(consume_s3 Rocida::S3 CONSUME_S3)
add_rocida_consumer(consume_cmeta Rocida::CMeta CONSUME_CMETA)
add_rocida_consumer(consume_cbind Rocida::CBind CONSUME_CBIND)
add_executable(consume_json_cbind consumer.c)
target_compile_features(consume_json_cbind PRIVATE c_std_11)
target_compile_definitions(consume_json_cbind PRIVATE CONSUME_JSON_CBIND=1)
target_link_libraries(consume_json_cbind PRIVATE
  Rocida::JsonCSerdeAdapter
  Rocida::CBind
  Rocida::Core)
add_rocida_consumer(consume_cflow Rocida::CFlow CONSUME_CFLOW)
add_rocida_consumer(consume_xml_parser Rocida::XmlParser
                       CONSUME_XML_PARSER)
add_rocida_consumer(consume_cflow_process Rocida::CFlowProcess
                       CONSUME_CFLOW_PROCESS)
add_rocida_consumer(consume_cflow_fs Rocida::CFlowFS CONSUME_CFLOW_FS)
add_rocida_consumer(consume_stl Rocida::STL CONSUME_STL)
add_rocida_consumer(consume_stl_stream Rocida::STLStream
                       CONSUME_STL_STREAM)
add_rocida_consumer(consume_core Rocida::Core CONSUME_CORE)

set(cflow_example_source_dir
    "${CMAKE_CURRENT_LIST_DIR}/../../cflow/examples")
foreach(cflow_example_name IN ITEMS
    cflow_native_socket_example
    cflow_native_pipe_example
    cflow_native_file_example)
  add_executable(consume_${cflow_example_name}
                 "${cflow_example_source_dir}/${cflow_example_name}.c")
  target_compile_features(consume_${cflow_example_name} PRIVATE c_std_11)
  target_link_libraries(consume_${cflow_example_name}
                        PRIVATE Rocida::CFlow)
endforeach()
if(WIN32)
  target_link_libraries(consume_cflow_native_socket_example PRIVATE ws2_32)
endif()

if(ROCIDA_EXPECT_CAPTURE)
  if(NOT TARGET Rocida::Capture)
    message(FATAL_ERROR "Rocida::Capture was requested but is not exported")
  endif()
  add_rocida_consumer(consume_capture Rocida::Capture CONSUME_CAPTURE)
elseif(TARGET Rocida::Capture)
  message(FATAL_ERROR
          "feature-off Rocida package unexpectedly exports Capture")
endif()

add_executable(consume_stl_cpp consumer.cpp)
target_compile_features(consume_stl_cpp PRIVATE cxx_std_17)
target_link_libraries(consume_stl_cpp PRIVATE Rocida::STL)

add_executable(consume_network_cpp consumer.cpp)
target_compile_features(consume_network_cpp PRIVATE cxx_std_17)
target_compile_definitions(consume_network_cpp PRIVATE CONSUME_NETWORK_CPP=1)
target_link_libraries(consume_network_cpp PRIVATE Rocida::CRPC)

add_executable(consume_s3_cpp consumer.cpp)
target_compile_features(consume_s3_cpp PRIVATE cxx_std_17)
target_compile_definitions(consume_s3_cpp PRIVATE CONSUME_S3_CPP=1)
target_link_libraries(consume_s3_cpp PRIVATE Rocida::S3)
