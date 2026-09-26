if(NOT DEFINED GENERATED OR NOT EXISTS "${GENERATED}")
  message(FATAL_ERROR "generated method-lowering source is missing")
endif()

file(READ "${GENERATED}" source)

string(FIND "${source}" "rc = IntList_add(&list, 10);" direct_call)
if(direct_call EQUAL -1)
  message(FATAL_ERROR "dot-call did not lower to direct IntList_add receiver call")
endif()

foreach(forbidden
    "cmeta_function_receiver("
    "cmeta_function_find_param("
    "cmeta_callable"
    "cmeta_invokable"
    "ops->"
    "(*")
  string(FIND "${source}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR "runtime dispatch token leaked into lowered source: ${forbidden}")
  endif()
endforeach()
