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
add_turboutils_consumer(consume_cflow_process TurboUtils::CFlowProcess
                       CONSUME_CFLOW_PROCESS)
add_turboutils_consumer(consume_cflow_fs TurboUtils::CFlowFS CONSUME_CFLOW_FS)
add_turboutils_consumer(consume_stl TurboUtils::STL CONSUME_STL)
add_turboutils_consumer(consume_stl_stream TurboUtils::STLStream
                       CONSUME_STL_STREAM)
add_turboutils_consumer(consume_core TurboUtils::Core CONSUME_CORE)

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

add_executable(consume_stl_cpp consumer.cpp)
target_compile_features(consume_stl_cpp PRIVATE cxx_std_17)
target_link_libraries(consume_stl_cpp PRIVATE TurboUtils::STL)
