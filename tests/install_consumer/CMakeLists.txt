cmake_minimum_required(VERSION 3.20)
project(TurboUtilsInstallConsumer LANGUAGES C CXX)

find_package(TurboUtils CONFIG REQUIRED)

function(add_turboutils_consumer name target contract)
  add_executable(${name} consumer.c)
  target_compile_features(${name} PRIVATE c_std_11)
  target_compile_definitions(${name} PRIVATE ${contract}=1)
  target_link_libraries(${name} PRIVATE ${target})
endfunction()

add_turboutils_consumer(consume_platform TurboUtils::Platform CONSUME_PLATFORM)
add_turboutils_consumer(consume_concurrency TurboUtils::Concurrency
                       CONSUME_CONCURRENCY)
add_turboutils_consumer(consume_cmeta TurboUtils::CMeta CONSUME_CMETA)
add_turboutils_consumer(consume_cbind TurboUtils::CBind CONSUME_CBIND)
add_executable(consume_json_cbind consumer.c)
target_compile_features(consume_json_cbind PRIVATE c_std_11)
target_compile_definitions(consume_json_cbind PRIVATE CONSUME_JSON_CBIND=1)
target_link_libraries(consume_json_cbind PRIVATE
  TurboUtils::JsonCSerdeAdapter
  TurboUtils::CBind
  TurboUtils::Core)
add_turboutils_consumer(consume_cflow TurboUtils::CFlow CONSUME_CFLOW)
add_turboutils_consumer(consume_xml_parser TurboUtils::XmlParser
                       CONSUME_XML_PARSER)
add_turboutils_consumer(consume_cflow_process TurboUtils::CFlowProcess
                       CONSUME_CFLOW_PROCESS)
add_turboutils_consumer(consume_cflow_fs TurboUtils::CFlowFS CONSUME_CFLOW_FS)
add_turboutils_consumer(consume_stl TurboUtils::STL CONSUME_STL)
add_turboutils_consumer(consume_stl_stream TurboUtils::STLStream
                       CONSUME_STL_STREAM)
add_turboutils_consumer(consume_core TurboUtils::Core CONSUME_CORE)

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
                        PRIVATE TurboUtils::CFlow)
endforeach()
if(WIN32)
  target_link_libraries(consume_cflow_native_socket_example PRIVATE ws2_32)
endif()

if(TURBOUTILS_EXPECT_CAPTURE)
  if(NOT TARGET TurboUtils::Capture)
    message(FATAL_ERROR "TurboUtils::Capture was requested but is not exported")
  endif()
  add_turboutils_consumer(consume_capture TurboUtils::Capture CONSUME_CAPTURE)
elseif(TARGET TurboUtils::Capture)
  message(FATAL_ERROR
          "feature-off TurboUtils package unexpectedly exports Capture")
endif()

if(TURBOUTILS_EXPECT_CFLOW_MINICORO)
  if(NOT TARGET TurboUtils::CFlowMinicoro)
    message(FATAL_ERROR
            "TurboUtils::CFlowMinicoro was requested but is not exported")
  endif()
  add_turboutils_consumer(consume_cflow_minicoro
                         TurboUtils::CFlowMinicoro
                         CONSUME_CFLOW_MINICORO)
  target_link_libraries(consume_cflow_minicoro PRIVATE TurboUtils::Core)
elseif(TARGET TurboUtils::CFlowMinicoro)
  message(FATAL_ERROR
          "feature-off TurboUtils package unexpectedly exports CFlowMinicoro")
endif()

if(TURBOUTILS_EXPECT_CFLOW_USB)
  if(NOT TARGET TurboUtils::CFlowUSB)
    message(FATAL_ERROR "TurboUtils::CFlowUSB was requested but is not exported")
  endif()
  add_turboutils_consumer(consume_cflow_usb
                         TurboUtils::CFlowUSB
                         CONSUME_CFLOW_USB)
elseif(TARGET TurboUtils::CFlowUSB)
  message(FATAL_ERROR
          "feature-off TurboUtils package unexpectedly exports CFlowUSB")
endif()

if(TURBOUTILS_EXPECT_CFLOW_SCXML)
  if(NOT TARGET TurboUtils::CFlowScxml)
    message(FATAL_ERROR
            "TurboUtils::CFlowScxml was requested but is not exported")
  endif()
  add_turboutils_consumer(consume_cflow_scxml
                         TurboUtils::CFlowScxml CONSUME_CFLOW_SCXML)
  add_custom_command(TARGET consume_cflow_scxml POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:consume_cflow_scxml>
            $<TARGET_FILE_DIR:consume_cflow_scxml>
    COMMAND $<TARGET_FILE:consume_cflow_scxml>
    COMMENT "Running installed CFlow SCXML consumer"
    COMMAND_EXPAND_LISTS)
elseif(TARGET TurboUtils::CFlowScxml)
  message(FATAL_ERROR
          "feature-off TurboUtils package unexpectedly exports CFlowScxml")
endif()

add_executable(consume_stl_cpp consumer.cpp)
target_compile_features(consume_stl_cpp PRIVATE cxx_std_17)
target_link_libraries(consume_stl_cpp PRIVATE TurboUtils::STL)
