cmake_minimum_required(VERSION 3.20)
project(TurboUtilsInstallConsumer LANGUAGES C)

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
add_turboutils_consumer(consume_cflow TurboUtils::CFlow CONSUME_CFLOW)
add_turboutils_consumer(consume_stl TurboUtils::STL CONSUME_STL)
add_turboutils_consumer(consume_core TurboUtils::Core CONSUME_CORE)
