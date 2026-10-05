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
    "ExternalList external_cleanup = {0}"
    "cmeta_data_value_destroy(IntList_cmeta_data(),&external_cleanup);"
    "IntList cleanup_first = {0}"
    "IntVec cleanup_second = {0}"
    "IntMap cleanup_map = {0}"
    "cmeta_data_value_destroy(IntMap_cmeta_data(),&cleanup_map);"
    "cmeta_data_value_destroy(IntVec_cmeta_data(),&cleanup_second);"
    "cmeta_data_value_destroy(IntList_cmeta_data(),&cleanup_first);"
    "IntList moved_source = {0}"
    "moved_sink = moved_source"
    "IntList transfer = {0}"
    "IntList_add(&transfer,60)"
    "inner_received = inner_transfer"
    "IntList_add(&transfer,61)"
    "received = transfer"
    "\"list.add(99); List_add(&list, 99);\""
    "\"owned(IntList) fake = {0}; move(fake);\""
    "cmeta_lower_defer_digit(&defer_order, 3);"
    "cmeta_lower_defer_digit(&defer_order, 2);"
    "cmeta_lower_defer_digit(&defer_order, 4);"
    "cmeta_lower_defer_digit(&defer_order, 1);"
    "IntList defer_owned_first = {0}"
    "IntVec defer_owned_second = {0}"
    "cmeta_lower_defer_digit(&defer_owned_order, 6);"
    "cmeta_lower_defer_digit(&defer_owned_order, 5);"
    "/* list.add(77); List_add(&list, 77); */"
    "/* defer cmeta_lower_defer_digit(&defer_order, 9); */"
    "/* owned(IntList) fake = {0}; move(fake); */")
  string(FIND "${source}" "${expected}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "expected lowered/preserved source fragment missing: ${expected}")
  endif()
endforeach()

string(REGEX MATCHALL
       "cmeta_data_value_destroy\\(IntList_cmeta_data\\(\\),&external_cleanup\\);"
       external_cleanup_matches "${source}")
list(LENGTH external_cleanup_matches external_cleanup_count)
if(NOT external_cleanup_count EQUAL 1)
  message(FATAL_ERROR "external lifecycle binding must clean up exactly once")
endif()

string(REGEX MATCHALL
       "cmeta_data_value_destroy\\(IntList_cmeta_data\\(\\),&cleanup_first\\);"
       cleanup_first_matches "${source}")
list(LENGTH cleanup_first_matches cleanup_first_count)
if(NOT cleanup_first_count EQUAL 1)
  message(FATAL_ERROR "cleanup_first must be destroyed exactly once")
endif()

string(REGEX MATCHALL
       "cmeta_data_value_destroy\\(IntVec_cmeta_data\\(\\),&cleanup_second\\);"
       cleanup_second_matches "${source}")
list(LENGTH cleanup_second_matches cleanup_second_count)
if(NOT cleanup_second_count EQUAL 1)
  message(FATAL_ERROR "cleanup_second must be destroyed exactly once")
endif()

string(REGEX MATCHALL
       "cmeta_data_value_destroy\\(IntMap_cmeta_data\\(\\),&cleanup_map\\);"
       cleanup_map_matches "${source}")
list(LENGTH cleanup_map_matches cleanup_map_count)
if(NOT cleanup_map_count EQUAL 1)
  message(FATAL_ERROR "cleanup_map must be destroyed exactly once")
endif()

string(FIND "${source}"
       "cmeta_data_value_destroy(IntMap_cmeta_data(),&cleanup_map);"
       cleanup_map_pos)
string(FIND "${source}"
       "cmeta_data_value_destroy(IntVec_cmeta_data(),&cleanup_second);"
       cleanup_second_pos)
string(FIND "${source}"
       "cmeta_data_value_destroy(IntList_cmeta_data(),&cleanup_first);"
       cleanup_first_pos)
if(cleanup_map_pos EQUAL -1 OR cleanup_second_pos EQUAL -1 OR
   cleanup_first_pos EQUAL -1 OR
   NOT cleanup_map_pos LESS cleanup_second_pos OR
   NOT cleanup_second_pos LESS cleanup_first_pos)
  message(FATAL_ERROR "owned cleanup must run in reverse declaration order")
endif()

string(FIND "${source}"
       "cmeta_lower_defer_digit(&defer_owned_order, 6);"
       defer_owned_6_pos)
string(FIND "${source}"
       "cmeta_data_value_destroy(IntVec_cmeta_data(),&defer_owned_second);"
       defer_owned_second_cleanup_pos)
string(FIND "${source}"
       "cmeta_lower_defer_digit(&defer_owned_order, 5);"
       defer_owned_5_pos)
string(FIND "${source}"
       "cmeta_data_value_destroy(IntList_cmeta_data(),&defer_owned_first);"
       defer_owned_first_cleanup_pos)
if(defer_owned_6_pos EQUAL -1 OR
   defer_owned_second_cleanup_pos EQUAL -1 OR
   defer_owned_5_pos EQUAL -1 OR
   defer_owned_first_cleanup_pos EQUAL -1 OR
   NOT defer_owned_6_pos LESS defer_owned_second_cleanup_pos OR
   NOT defer_owned_second_cleanup_pos LESS defer_owned_5_pos OR
   NOT defer_owned_5_pos LESS defer_owned_first_cleanup_pos)
  message(FATAL_ERROR
          "defer and owned cleanup must share one reverse lexical LIFO order")
endif()

string(FIND "${source}"
       "cmeta_data_value_destroy(IntList_cmeta_data(),&moved_source);"
       moved_cleanup)
if(NOT moved_cleanup EQUAL -1)
  message(FATAL_ERROR "moved owned source unexpectedly receives automatic cleanup")
endif()

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
