set(TARGET_NAME salts_platform)

if(WIN32)
  set(SALTS_PLATFORM_THREAD_SOURCE src/thread_win32.c)
else()
  set(SALTS_PLATFORM_THREAD_SOURCE src/thread_posix.c)
endif()

add_library(${TARGET_NAME} STATIC
  src/clock.c
  src/fastpath.c
  src/random.c
  src/readiness.c
  ${SALTS_PLATFORM_THREAD_SOURCE})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   SALTS_ENABLE_EPOLL_READINESS)
  target_sources(${TARGET_NAME} PRIVATE src/readiness_epoll.c)
  target_compile_definitions(${TARGET_NAME}
    PRIVATE SALTS_ENABLE_EPOLL_READINESS=1)
elseif(APPLE)
  target_sources(${TARGET_NAME} PRIVATE src/readiness_kqueue.c)
  target_compile_definitions(${TARGET_NAME}
    PRIVATE SALTS_ENABLE_KQUEUE_READINESS=1)
endif()

if(UNIX)
  target_sources(${TARGET_NAME} PRIVATE src/readiness_poll.c)
  target_compile_definitions(${TARGET_NAME}
    PRIVATE SALTS_ENABLE_POLL_READINESS=1)
endif()

cmake_config_target(
  ${TARGET_NAME}
  ALIAS Salts::Platform
  FOLDER "platform"
  EXPORT_NAME Platform)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
if(SALTS_NATIVE_FASTPATH)
  if(ENABLE_SANITIZER_THREAD)
    message(FATAL_ERROR "SALTS_NATIVE_FASTPATH assembly loads cannot be qualified by ThreadSanitizer; use the portable C reference")
  endif()
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "SALTS_NATIVE_FASTPATH requires a qualified 64-bit ABI")
  endif()
  set(salts_fastpath_processor "${CMAKE_SYSTEM_PROCESSOR}")
  if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES salts_architecture_count)
    if(NOT salts_architecture_count EQUAL 1)
      message(FATAL_ERROR "SALTS_NATIVE_FASTPATH requires one explicitly qualified macOS architecture")
    endif()
    set(salts_fastpath_processor "${CMAKE_OSX_ARCHITECTURES}")
  endif()
  if(salts_fastpath_processor MATCHES "^(AMD64|amd64|x86_64|X86_64)$")
    if(WIN32)
      enable_language(ASM_MASM)
      target_sources(${TARGET_NAME} PRIVATE arch/windows-x64/fastpath.asm)
    elseif(UNIX)
      enable_language(ASM)
      target_sources(${TARGET_NAME} PRIVATE arch/x86_64/fastpath.S)
    else()
      message(FATAL_ERROR "SALTS_NATIVE_FASTPATH has no qualified x86_64 OS backend")
    endif()
  elseif(salts_fastpath_processor MATCHES "^(aarch64|arm64|ARM64)$" AND UNIX)
    enable_language(ASM)
    target_sources(${TARGET_NAME} PRIVATE arch/aarch64/fastpath.S)
  else()
    message(FATAL_ERROR "SALTS_NATIVE_FASTPATH has no qualified backend for ${CMAKE_SYSTEM_NAME}/${salts_fastpath_processor}")
  endif()
  target_compile_definitions(${TARGET_NAME} PUBLIC SALTS_NATIVE_FASTPATH=1)
endif()
target_compile_definitions(${TARGET_NAME} PRIVATE
  "$<$<COMPILE_LANGUAGE:ASM>:SALTS_FASTPATH_STATIC_LIBRARY=1>")
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(${TARGET_NAME} PUBLIC Threads::Threads)

if(WIN32)
  target_link_libraries(${TARGET_NAME} PRIVATE bcrypt)
endif()

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
