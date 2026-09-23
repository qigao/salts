# TinyMock usage boundary.
#
# Phase 1 keeps the existing runtime objects in Salts::TinyTest for
# source/binary compatibility while exposing the final consumer dependency:
#
#   Salts::TinyMock -> Salts::TinyTest + Salts::CMeta
#
# A later migration may move tinymock.c into a dedicated archive without
# changing the public Salts::TinyMock target contract.

add_library(tinymock STATIC
  src/tinymock_history.c
  src/tinymock_actions.c
  src/tinymock_return.c)

cmake_config_target(tinymock
  ALIAS Salts::TinyMock
  FOLDER "tinymock"
  EXPORT_NAME TinyMock)

target_compile_features(tinymock PUBLIC c_std_11)

target_link_libraries(tinymock
  PUBLIC
    Salts::TinyTest
    Salts::CMeta)

target_include_directories(tinymock
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

install(
  TARGETS tinymock
  EXPORT SaltsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  COMPONENT TinyMock)

install(
  FILES
    include/tinymock_cmeta.h
    include/tinymock_function.h
    include/tinymock_history.h
    include/tinymock_actions.h
    include/tinymock_return.h
  DESTINATION include
  COMPONENT TinyMock)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/SaltsTinyMock.cmake)

configure_file(
  cmake/SaltsTinyMock.cmake
  ${CMAKE_BINARY_DIR}/SaltsTinyMock.cmake
  COPYONLY)

install(
  FILES cmake/SaltsTinyMock.cmake
  DESTINATION lib/cmake/Salts
  COMPONENT TinyMock)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
