set(TARGET_NAME turbo_stream)

add_library(${TARGET_NAME}
  stream.c
  stream_container.c
  stream_live.c
  stream_spsc.c
  stream_turbo_containers.c)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS TurboUtils::Stream
  FOLDER "stream"
  EXPORT_NAME Stream)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(${TARGET_NAME} PUBLIC TurboUtils::Core)

install(
  TARGETS ${TARGET_NAME}
  EXPORT TurboUtilsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  FILES stream.h stream_container.h stream_live.h stream_streamable.h stream_typed.h
        stream_spsc.h stream_turbo_containers.h stream.hpp
  DESTINATION include)

cmake_add_test(test_stream
  SOURCES tests/test_stream.c
  LIBS TurboUtils::TinyTest TurboUtils::Stream
  FOLDER "stream/tests")

cmake_add_test(test_stream_typed
  SOURCES tests/test_stream_typed.c
  LIBS TurboUtils::TinyTest TurboUtils::Stream
  FOLDER "stream/tests")

cmake_add_test(test_stream_streamable
  SOURCES tests/test_stream_streamable.c
  LIBS TurboUtils::TinyTest TurboUtils::Stream
  FOLDER "stream/tests")

cmake_add_test(test_stream_cpp
  SOURCES tests/test_stream_cpp.cpp
  LIBS TurboUtils::TinyTest TurboUtils::Stream
  FOLDER "stream/tests")

cmake_add_test(test_stream_turbo_containers
  SOURCES tests/test_stream_turbo_containers.c
  LIBS TurboUtils::TinyTest TurboUtils::Stream
  FOLDER "stream/tests")

set_target_properties(test_stream_cpp PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON)

add_subdirectory(examples)
