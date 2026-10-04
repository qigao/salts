if(NOT DEFINED GENERATED OR NOT EXISTS "${GENERATED}")
  message(FATAL_ERROR "generated inspection source is missing")
endif()

file(READ "${GENERATED}" content)

foreach(required
    "#include \"cmeta_inspection_fixture.h\""
    "cmeta_inspection_increment(value.number)"
    "cmeta_inspection_record value")
  string(FIND "${content}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR
      "generated inspection source is missing direct-C witness: ${required}")
  endif()
endforeach()

foreach(forbidden
    "#include <cmeta/"
    "cmeta_function_desc"
    "cmeta_function_param"
    "cmeta_receiver_method_find"
    "cmeta_receiver_method_resolve"
    "cmeta_interface_desc"
    "cmeta_invokable"
    "cmeta_callable"
    "cmeta_data_desc"
    "cmeta_declared_type")
  string(FIND "${content}" "${forbidden}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR
      "generated runtime source leaked reflection/control-plane token: ${forbidden}")
  endif()
endforeach()
