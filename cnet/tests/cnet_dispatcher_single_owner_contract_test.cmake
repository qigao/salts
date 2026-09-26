if(NOT DEFINED PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(dispatcher_source "${PROJECT_SOURCE_DIR}/cnet/src/cnet_dispatcher.c")
file(READ "${dispatcher_source}" dispatcher)

string(FIND "${dispatcher}" "salts_mutex_" mutex_call)
if(NOT mutex_call EQUAL -1)
  message(FATAL_ERROR "CNet single-owner dispatcher must not call salts_mutex_*")
endif()

string(FIND "${dispatcher}" "salts_mutex_t" mutex_type)
if(NOT mutex_type EQUAL -1)
  message(FATAL_ERROR "CNet single-owner dispatcher must not own a mutex")
endif()

foreach(marker
    "cnet_dispatcher_prepare"
    "cnet_dispatcher_recycle"
    "cnet_dispatcher_register"
    "cnet_dispatcher_drain")
  string(FIND "${dispatcher}" "${marker}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "CNet dispatcher contract marker missing: ${marker}")
  endif()
endforeach()

message(STATUS "CNet dispatcher single-owner lock-free contract passed")
