if(NOT DEFINED GENERATED OR NOT EXISTS "${GENERATED}")
  message(FATAL_ERROR "generated method-lowering source is missing")
endif()
if(NOT DEFINED EXPECTED_CALL)
  message(FATAL_ERROR "expected direct typed call is missing")
endif()

file(READ "${GENERATED}" source)

string(FIND "${source}" "${EXPECTED_CALL}" direct_call)
if(direct_call EQUAL -1)
  message(FATAL_ERROR
          "dot-call did not lower to expected direct typed call: ${EXPECTED_CALL}")
endif()

foreach(forbidden
    "cmeta_function_receiver("
    "cmeta_function_find_param("
    "cmeta_callable"
    "cmeta_invokable"
    "cmeta_receiver_method"
    "receiver_method("
    "ops->"
    "(*")
  string(FIND "${source}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
            "runtime dispatch token leaked into lowered source: ${forbidden}")
  endif()
endforeach()
