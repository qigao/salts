# TinyMock is the CMeta-aware mocking component.
#
#   Salts::TinyMock -> Salts::TinyTest + Salts::CMeta
#
# TinyTest owns no mock runtime or reflection schema. TinyMock owns only
# reflected history/actions/returns and consumes canonical CMeta metadata.

add_library(tinymock STATIC
  src/tinymock_value.c
  src/tinymock_history.c
  src/tinymock_actions.c
  src/tinymock_return.c)

cmake_config_target(tinymock
  VERSION "${PROJECT_VERSION}" SOVERSION "${PROJECT_VERSION_MAJOR}"
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
    include/tinymock.h
    include/tinymock_support.h
    include/tinymock_cmeta.h
    include/tinymock_value.h
    include/tinymock_function.h
    include/tinymock_history.h
    include/tinymock_actions.h
    include/tinymock_return.h
  DESTINATION include
  COMPONENT TinyMock)
