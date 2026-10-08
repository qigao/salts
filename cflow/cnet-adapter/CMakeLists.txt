set(TARGET_NAME cmeta_cflow_cnet)

add_library(${TARGET_NAME}
  io_cnet_adapter.c
  cnet_domain_actor.c
  cnet_domain_route.c)

cmake_config_target(
  ${TARGET_NAME}
  VERSION "${PROJECT_VERSION}" SOVERSION "${PROJECT_VERSION_MAJOR}"
  ALIAS Salts::CFlowCNet
  FOLDER "cflow/cnet-adapter"
  EXPORT_NAME CFlowCNet)

set_target_properties(${TARGET_NAME} PROPERTIES
  WINDOWS_EXPORT_ALL_SYMBOLS ON)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/cflow/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(
  ${TARGET_NAME}
  PUBLIC Salts::CFlow Salts::CNet)

install(
  TARGETS ${TARGET_NAME}
  EXPORT SaltsTargets)

# An explicitly opt-in manager/Domain Actor lifecycle adapter. Keep the
# ordinary Salts::CFlowCNet transport bridge independent of CNetManager.
add_library(cmeta_cflow_cnet_manager cnet_manager_context.c)
cmake_config_target(
  cmeta_cflow_cnet_manager
  VERSION "${PROJECT_VERSION}" SOVERSION "${PROJECT_VERSION_MAJOR}"
  ALIAS Salts::CFlowCNetManager
  FOLDER "cflow/cnet-adapter"
  EXPORT_NAME CFlowCNetManager)
set_target_properties(cmeta_cflow_cnet_manager PROPERTIES
  WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_features(cmeta_cflow_cnet_manager PUBLIC c_std_11)
target_include_directories(
  cmeta_cflow_cnet_manager
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/cflow/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(
  cmeta_cflow_cnet_manager
  PUBLIC Salts::CFlowCNet Salts::CNetManager)
install(TARGETS cmeta_cflow_cnet_manager EXPORT SaltsTargets)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

# Explicitly opt-in diagnostic workload; no release/test gate or perf threshold.
option(CFLOW_CNET_BUILD_BENCHMARKS "Build CNet/Domain Actor comparison benchmarks" OFF)
if(CFLOW_CNET_BUILD_BENCHMARKS)
  if(NOT (WIN32 OR APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    message(FATAL_ERROR "CFlow CNet TCP benchmark supports Windows, Darwin or Linux hosts")
  endif()
  add_executable(cflow_cnet_direct_tcp_benchmark
    benchmarks/cnet_direct_tcp_benchmark.c
    benchmarks/cnet_benchmark_stats.c)
  target_include_directories(cflow_cnet_direct_tcp_benchmark PRIVATE benchmarks)
  target_link_libraries(cflow_cnet_direct_tcp_benchmark PRIVATE Salts::CNet Salts::Platform)
  target_compile_features(cflow_cnet_direct_tcp_benchmark PRIVATE c_std_11)
  set_target_properties(cflow_cnet_direct_tcp_benchmark PROPERTIES
    C_EXTENSIONS OFF FOLDER "cflow/cnet-adapter/benchmarks")
  if(WIN32)
    target_link_libraries(cflow_cnet_direct_tcp_benchmark PRIVATE ws2_32)
  endif()
  add_executable(cflow_cnet_same_owner_actor_benchmark
    benchmarks/cnet_same_owner_actor_benchmark.c
    benchmarks/cnet_benchmark_stats.c)
  target_include_directories(cflow_cnet_same_owner_actor_benchmark PRIVATE benchmarks)
  target_link_libraries(cflow_cnet_same_owner_actor_benchmark PRIVATE
    Salts::CFlowCNet Salts::Platform)
  target_compile_features(cflow_cnet_same_owner_actor_benchmark PRIVATE c_std_11)
  set_target_properties(cflow_cnet_same_owner_actor_benchmark PROPERTIES
    C_EXTENSIONS OFF FOLDER "cflow/cnet-adapter/benchmarks")
  if(WIN32)
    target_link_libraries(cflow_cnet_same_owner_actor_benchmark PRIVATE ws2_32)
  endif()

endif()
