set(TARGET_NAME salts_native_io)

add_library(${TARGET_NAME} STATIC src/native_io.c)

if(WIN32)
  target_sources(${TARGET_NAME} PRIVATE src/native_io_iocp.c)
  target_compile_definitions(${TARGET_NAME} PRIVATE
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0600)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(
    ${TARGET_NAME}
    PRIVATE src/native_io_linux.c
            src/native_io_epoll.c
            src/native_io_io_uring.c
            src/native_io_readiness.c)
elseif((APPLE OR CMAKE_SYSTEM_NAME MATCHES "^(FreeBSD|OpenBSD|NetBSD|DragonFly)$")
       AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  target_sources(
    ${TARGET_NAME}
    PRIVATE src/native_io_bsd.c
            src/native_io_kqueue.c
            src/native_io_readiness.c)
  if(APPLE)
    target_compile_definitions(${TARGET_NAME} PRIVATE _DARWIN_C_SOURCE=1)
  endif()
else()
  target_sources(${TARGET_NAME} PRIVATE src/native_io_unsupported.c)
endif()

cmake_config_target(
  ${TARGET_NAME}
  ALIAS Salts::NativeIO
  FOLDER "native-io"
  EXPORT_NAME NativeIO)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(${TARGET_NAME}
  PUBLIC Salts::Platform
  PRIVATE Salts::Coroutine)

if(WIN32)
  target_link_libraries(${TARGET_NAME} PRIVATE ws2_32)
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

if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
