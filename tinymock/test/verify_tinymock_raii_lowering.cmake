if(NOT DEFINED GENERATED OR NOT EXISTS "${GENERATED}")
  message(FATAL_ERROR "generated TinyMock RAII source is missing")
endif()

file(READ "${GENERATED}" source)

foreach(expected
    "tinymock_tinymock_raii_probe mock = {0}"
    "cmeta_data_value_destroy(tinymock_tinymock_raii_probe_cmeta_data(),&mock);")
  string(FIND "${source}" "${expected}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "expected TinyMock RAII lowering fragment missing: ${expected}")
  endif()
endforeach()

string(REGEX MATCHALL
       "cmeta_data_value_destroy\\(tinymock_tinymock_raii_probe_cmeta_data\\(\\),&mock\\);"
       cleanup_matches "${source}")
list(LENGTH cleanup_matches cleanup_count)
if(NOT cleanup_count EQUAL 1)
  message(FATAL_ERROR "TinyMock reflected mock must be destroyed exactly once")
endif()

foreach(forbidden
    "tinymock_tinymock_raii_probe_destroy(&mock)"
    "__attribute__((cleanup"
    "__declspec")
  string(FIND "${source}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR "non-canonical cleanup leaked into lowered TinyMock source: ${forbidden}")
  endif()
endforeach()
