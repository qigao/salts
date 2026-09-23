# TinyMock usage boundary.
#
# Phase 1 keeps the existing runtime objects in Salts::TinyTest for binary/source
# compatibility while exposing the final dependency direction to consumers:
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

install(
  TARGETS tinymock
  EXPORT SaltsTargets
  COMPONENT TinyTest)
