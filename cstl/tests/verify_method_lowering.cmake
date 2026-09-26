if(NOT DEFINED GENERATED OR NOT EXISTS "${GENERATED}")
  message(FATAL_ERROR "generated method-lowering source is missing")
endif()
if(NOT DEFINED EXPECTED_CANONICAL)
  message(FATAL_ERROR "expected canonical generic operation is missing")
endif()
if(NOT DEFINED EXPECTED_CALL)
  message(FATAL_ERROR "expected native direct call is missing")
endif()

file(READ "${GENERATED}" source)

set(canonical_marker "/* canonical operation: ${EXPECTED_CANONICAL} */")
string(FIND "${source}" "${canonical_marker}" canonical_call)
if(canonical_call EQUAL -1)
  message(FATAL_ERROR
          "source did not normalize to expected generic operation: ${EXPECTED_CANONICAL}")
endif()

string(FIND "${source}" "${EXPECTED_CALL}" direct_call)
if(direct_call EQUAL -1)
  message(FATAL_ERROR
          "canonical operation did not materialize expected native call: ${EXPECTED_CALL}")
endif()

foreach(forbidden
    "cmeta_function_receiver("
    "cmeta_function_find_param("
    "cmeta_callable"
    "cmeta_invokable"
    "cmeta_receiver_method"
    "cmeta_receiver_method_resolve"
    "receiver_method("
    "ops->"
    "(*")
  string(FIND "${source}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR
            "runtime dispatch token leaked into lowered source: ${forbidden}")
  endif()
endforeach()
