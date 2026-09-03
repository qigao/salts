cmake_minimum_required(VERSION 3.20)
project(SaltsInstallConsumer LANGUAGES C CXX)

find_package(Salts CONFIG REQUIRED)

function(add_salts_consumer name target contract)
  add_executable(${name} consumer.c)
  target_compile_features(${name} PRIVATE c_std_11)
  target_compile_definitions(${name} PRIVATE ${contract}=1)
  target_link_libraries(${name} PRIVATE ${target})
endfunction()

add_salts_consumer(consume_platform Salts::Platform CONSUME_PLATFORM)
add_salts_consumer(consume_crypto Salts::Crypto CONSUME_CRYPTO)
add_salts_consumer(consume_concurrency Salts::Concurrency
                       CONSUME_CONCURRENCY)
add_salts_consumer(consume_coroutine Salts::Coroutine
                       CONSUME_COROUTINE)
add_salts_consumer(consume_native_io Salts::NativeIO
                       CONSUME_NATIVE_IO)
add_salts_consumer(consume_cnet Salts::CNet CONSUME_CNET)
add_salts_consumer(consume_chttp Salts::CHTTP CONSUME_CHTTP)
add_salts_consumer(consume_crpc Salts::CRPC CONSUME_CRPC)
add_salts_consumer(consume_s3 Salts::S3 CONSUME_S3)
add_salts_consumer(consume_cmeta Salts::CMeta CONSUME_CMETA)
add_salts_consumer(consume_cbind Salts::CBind CONSUME_CBIND)
add_executable(consume_json_cbind consumer.c)
target_compile_features(consume_json_cbind PRIVATE c_std_11)
target_compile_definitions(consume_json_cbind PRIVATE CONSUME_JSON_CBIND=1)
target_link_libraries(consume_json_cbind PRIVATE
  Salts::JsonCSerdeAdapter
  Salts::CBind
  Salts::Core)
add_salts_consumer(consume_cflow Salts::CFlow CONSUME_CFLOW)
add_salts_consumer(consume_xml_parser Salts::XmlParser
                       CONSUME_XML_PARSER)
add_salts_consumer(consume_cflow_process Salts::CFlowProcess
                       CONSUME_CFLOW_PROCESS)
add_salts_consumer(consume_cflow_fs Salts::CFlowFS CONSUME_CFLOW_FS)
add_salts_consumer(consume_cstl Salts::CSTL CONSUME_CSTL)
add_salts_consumer(consume_cstl_stream Salts::CSTLStream
                       CONSUME_CSTL_STREAM)
add_salts_consumer(consume_core Salts::Core CONSUME_CORE)

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
                        PRIVATE Salts::CFlow)
endforeach()
if(WIN32)
  target_link_libraries(consume_cflow_native_socket_example PRIVATE ws2_32)
endif()

if(SALTS_EXPECT_CAPTURE)
  if(NOT TARGET Salts::Capture)
    message(FATAL_ERROR "Salts::Capture was requested but is not exported")
  endif()
  add_salts_consumer(consume_capture Salts::Capture CONSUME_CAPTURE)
elseif(TARGET Salts::Capture)
  message(FATAL_ERROR
          "feature-off Salts package unexpectedly exports Capture")
endif()

add_executable(consume_cstl_cpp consumer.cpp)
target_compile_features(consume_cstl_cpp PRIVATE cxx_std_17)
target_link_libraries(consume_cstl_cpp PRIVATE Salts::CSTL)

add_executable(consume_network_cpp consumer.cpp)
target_compile_features(consume_network_cpp PRIVATE cxx_std_17)
target_compile_definitions(consume_network_cpp PRIVATE CONSUME_NETWORK_CPP=1)
target_link_libraries(consume_network_cpp PRIVATE Salts::CRPC)

add_executable(consume_s3_cpp consumer.cpp)
target_compile_features(consume_s3_cpp PRIVATE cxx_std_17)
target_compile_definitions(consume_s3_cpp PRIVATE CONSUME_S3_CPP=1)
target_link_libraries(consume_s3_cpp PRIVATE Salts::S3)
