set(TARGET_NAME turbo_platform)

if(WIN32)
  set(TURBO_PLATFORM_THREAD_SOURCE src/thread_win32.c)
else()
  set(TURBO_PLATFORM_THREAD_SOURCE src/thread_posix.c)
endif()

add_library(${TARGET_NAME} STATIC
  src/clock.c
  src/readiness.c
  ${TURBO_PLATFORM_THREAD_SOURCE})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   TURBO_ENABLE_EPOLL_READINESS)
  target_sources(${TARGET_NAME} PRIVATE src/readiness_epoll.c)
  target_compile_definitions(${TARGET_NAME}
    PRIVATE TURBO_ENABLE_EPOLL_READINESS=1)
elseif(APPLE)
  target_sources(${TARGET_NAME} PRIVATE src/readiness_kqueue.c)
  target_compile_definitions(${TARGET_NAME}
    PRIVATE TURBO_ENABLE_KQUEUE_READINESS=1)
endif()

cmake_config_target(
  ${TARGET_NAME}
  ALIAS TurboUtils::Platform
  FOLDER "platform"
  EXPORT_NAME Platform)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(${TARGET_NAME} PUBLIC Threads::Threads)

if(APPLE)
  target_compile_definitions(${TARGET_NAME} PRIVATE _DARWIN_C_SOURCE=1)
elseif(UNIX)
  target_compile_definitions(
    ${TARGET_NAME}
    PRIVATE _DEFAULT_SOURCE=1 _POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700)
endif()

install(
  TARGETS ${TARGET_NAME}
  EXPORT TurboUtilsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  DIRECTORY include/turbo
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
