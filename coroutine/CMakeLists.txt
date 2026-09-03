set(TARGET_NAME salts_coroutine)

add_library(
  ${TARGET_NAME} STATIC
  src/salts_coro.c
  src/salts_coro_pool.c
  src/salts_coro_executor.c)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS Salts::Coroutine
  FOLDER "coroutine"
  EXPORT_NAME Coroutine)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${PROJECT_SOURCE_DIR}/vendor/minicoro)
target_link_libraries(${TARGET_NAME} PUBLIC Salts::Concurrency)

install(
  TARGETS ${TARGET_NAME}
  EXPORT SaltsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  DIRECTORY include/
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
