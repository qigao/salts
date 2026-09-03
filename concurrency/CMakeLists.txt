set(TARGET_NAME salts_concurrency)

add_library(${TARGET_NAME} STATIC
  src/deadline_queue.c
  src/disruptor.c
  src/spsc_ring.c
  src/thread_pool.c)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS Salts::Concurrency
  FOLDER "concurrency"
  EXPORT_NAME Concurrency)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(${TARGET_NAME} PUBLIC Salts::Platform)

if(APPLE)
  target_compile_definitions(${TARGET_NAME} PRIVATE _DARWIN_C_SOURCE=1)
elseif(UNIX)
  target_compile_definitions(
    ${TARGET_NAME}
    PRIVATE _DEFAULT_SOURCE=1 _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700)
endif()

install(
  TARGETS ${TARGET_NAME}
  EXPORT SaltsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  DIRECTORY include/salts
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
