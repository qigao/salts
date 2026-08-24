set(TARGET_NAME turbo_cflow)

set(CFLOW_MACHINE_MAX_STATES 65536 CACHE STRING
    "Maximum state declarations accepted by one CFlow Machine")
set(CFLOW_MACHINE_MAX_EVENTS 65536 CACHE STRING
    "Maximum Event declarations accepted by one CFlow Machine")
set(CFLOW_MACHINE_MAX_GUARDS 65536 CACHE STRING
    "Maximum guard declarations accepted by one CFlow Machine")
set(CFLOW_MACHINE_MAX_ACTIONS 65536 CACHE STRING
    "Maximum action declarations accepted by one CFlow Machine")
set(CFLOW_MACHINE_MAX_TRANSITIONS 1048576 CACHE STRING
    "Maximum transitions accepted by one CFlow Machine")
foreach(machine_limit IN ITEMS
    CFLOW_MACHINE_MAX_STATES
    CFLOW_MACHINE_MAX_EVENTS
    CFLOW_MACHINE_MAX_GUARDS
    CFLOW_MACHINE_MAX_ACTIONS
    CFLOW_MACHINE_MAX_TRANSITIONS)
  if(NOT "${${machine_limit}}" MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "${machine_limit} must be a positive integer")
  endif()
  if(${machine_limit} GREATER 1048576)
    message(FATAL_ERROR
      "${machine_limit} must not exceed the verified ceiling 1048576")
  endif()
endforeach()

add_library(${TARGET_NAME}
  src/adapters.c
  src/actor.c
  src/certificate.c
  src/clock.c
  src/coord.c
  src/direct.c
  src/dense_successor_index.c
  src/effect.c
  src/event.c
  src/executor.c
  src/graph.c
  src/interfaces.c
  src/lower.c
  src/machine.c
  src/machine_runtime.c
  src/opt.c
  src/plan_compile.c
  src/plan_exec.c
  src/plan_parallel_reduce.c
  src/property.c
  src/reactive.c
  src/relation_exec.c
  src/runtime.c
  src/scheduler.c
  src/scheduler_worker.c
  src/sources.c
  src/stream.c
  src/subrun.c
  src/timer_queue.c
  src/timer_event.c
  src/verify.c)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS TurboUtils::CFlow
  FOLDER "cflow"
  EXPORT_NAME CFlow)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_compile_definitions(
  ${TARGET_NAME}
  PUBLIC
    CFLOW_MACHINE_MAX_STATES=${CFLOW_MACHINE_MAX_STATES}
    CFLOW_MACHINE_MAX_EVENTS=${CFLOW_MACHINE_MAX_EVENTS}
    CFLOW_MACHINE_MAX_GUARDS=${CFLOW_MACHINE_MAX_GUARDS}
    CFLOW_MACHINE_MAX_ACTIONS=${CFLOW_MACHINE_MAX_ACTIONS}
    CFLOW_MACHINE_MAX_TRANSITIONS=${CFLOW_MACHINE_MAX_TRANSITIONS})
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(
  ${TARGET_NAME}
  PUBLIC TurboUtils::CMeta
  PRIVATE TurboUtils::Platform TurboUtils::Concurrency)

install(
  TARGETS ${TARGET_NAME}
  EXPORT TurboUtilsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  DIRECTORY include/cflow
  DESTINATION include
  FILES_MATCHING
    PATTERN "*.h"
    PATTERN "plan_internal.h" EXCLUDE
    PATTERN "relation_exec.h" EXCLUDE)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
