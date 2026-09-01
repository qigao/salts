set(TARGET_NAME turbo_coroutine)

add_library(
  ${TARGET_NAME} STATIC
  src/turbo_coro.c
  src/turbo_coro_pool.c)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS TurboUtils::Coroutine
  FOLDER "coroutine"
  EXPORT_NAME Coroutine)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${PROJECT_SOURCE_DIR}/vendor/minicoro)

install(
  TARGETS ${TARGET_NAME}
  EXPORT TurboUtilsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  DIRECTORY include/
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")
