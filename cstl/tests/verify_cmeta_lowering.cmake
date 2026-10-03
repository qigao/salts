if(NOT DEFINED GENERATED OR NOT EXISTS "${GENERATED}")
  message(FATAL_ERROR "generated CMeta source is missing")
endif()

file(READ "${GENERATED}" source)

foreach(expected
    "IntList_add(&list,10)"
    "IntList_add(&list, 20)"
    "IntVec_push(&vec,10)"
    "IntVec_push(&vec, 20)"
    "IntSet_add(&set,10)"
    "IntSet_add(&set, 20)"
    "IntMap_put(&map,1, 10)"
    "IntMap_put(&map, 2, 20)"
    "IntVec_push(&list,30)"
    "IntVec_push(&list, 40)"
    "IntList_add(&list,50)"
    "IntList transfer = {0}"
    "IntList_add(&transfer,60)"
    "inner_received = transfer"
    "IntList_add(&transfer,61)"
    "received = transfer"
    "IntList automatic = {0}"
    "IntList first = {0}"
    "IntList second = {0}"
    "cmeta_data_value_restore_zero(IntList_cmeta_data(), &automatic);"
    "cmeta_data_value_restore_zero(IntList_cmeta_data(), &second);"
    "cmeta_data_value_restore_zero(IntList_cmeta_data(), &first);"
    "\"list.add(99); List_add(&list, 99);\""
    "\"owned(IntList) fake = {0}; move(fake);\""
    "/* list.add(77); List_add(&list, 77); */"
    "/* owned(IntList) fake = {0}; move(fake); */")
  string(FIND "${source}" "${expected}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "expected lowered/preserved source fragment missing: ${expected}")
  endif()
endforeach()

foreach(forbidden
    "cmeta_receiver_method_resolve"
    "cmeta_receiver_method_find"
    "cmeta_callable"
    "cmeta_invokable"
    "ops->")
  string(FIND "${source}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR "runtime dispatch token leaked into generated C: ${forbidden}")
  endif()
endforeach()

string(FIND "${source}"
  "cmeta_data_value_restore_zero(IntList_cmeta_data(), &second);"
  second_cleanup)
string(FIND "${source}"
  "cmeta_data_value_restore_zero(IntList_cmeta_data(), &first);"
  first_cleanup)
if(second_cleanup EQUAL -1 OR first_cleanup EQUAL -1 OR
   NOT second_cleanup LESS first_cleanup)
  message(FATAL_ERROR
    "owned cleanup must be emitted in reverse declaration order")
endif()
