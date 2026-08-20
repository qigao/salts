set(TARGET_NAME turbo_cflow)

add_library(${TARGET_NAME}
  src/adapters.c
  src/coord.c
  src/effect.c
  src/graph.c
  src/interfaces.c
  src/lower.c
  src/opt.c
  src/plan_compile.c
  src/plan_exec.c
  src/property.c
  src/reactive.c
  src/relation_exec.c
  src/runtime.c
  src/scheduler.c
  src/scheduler_worker.c
  src/sources.c
  src/stream.c
  src/subrun.c
  src/verify.c)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS TurboUtils::CFlow
  FOLDER "cflow"
  EXPORT_NAME CFlow)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(
  ${TARGET_NAME}
  PUBLIC TurboUtils::CMeta
  PRIVATE Threads::Threads)

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
