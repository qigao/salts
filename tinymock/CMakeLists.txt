# TinyMock usage boundary.
#
# Phase 1 keeps the existing runtime objects in Salts::TinyTest for
# source/binary compatibility while exposing the final consumer dependency:
#
#   Salts::TinyMock -> Salts::TinyTest + Salts::CMeta
#
# A later migration may move tinymock.c into a dedicated archive without
# changing the public Salts::TinyMock target contract.

add_library(tinymock INTERFACE)
add_library(Salts::TinyMock ALIAS tinymock)

set_target_properties(tinymock PROPERTIES
  EXPORT_NAME TinyMock)

target_link_libraries(tinymock
  INTERFACE
    Salts::TinyTest
    Salts::CMeta)

target_include_directories(tinymock
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

install(
  TARGETS tinymock
  EXPORT SaltsTargets
  COMPONENT TinyMock)

install(
  FILES include/tinymock_cmeta.h
  DESTINATION include
  COMPONENT TinyMock)

if(BUILD_TESTS)
  add_subdirectory(test)
endif()
