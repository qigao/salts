cmake_minimum_required(VERSION 3.25)

file(READ "${PROJECT_SOURCE_DIR}/cnet/include/cnet/cnet.h" _header)
file(READ "${PROJECT_SOURCE_DIR}/cnet/src/cnet_client.c" _client)

foreach(_required IN ITEMS
    "typedef void (*cnet_receive_slice_fn)"
    "int cnet_set_receive_slice_handler("
    "mem_get_buffer(mem_global(), view->size)"
    "record->receive_slice_handler(record->receive_slice_user"
    "record->observer.on_receive == NULL && record->receive_slice_handler == NULL")
  string(FIND "${_header}\n${_client}" "${_required}" _index)
  if(_index EQUAL -1)
    message(FATAL_ERROR
      "CNet owned receive contract is missing required fragment: ${_required}")
  endif()
endforeach()

string(FIND "${_header}" "typedef struct cnet_observer {" _observer_index)
string(FIND "${_header}" "cnet_receive_slice_fn receive_slice_handler" _observer_owned_index)
if(_observer_index EQUAL -1)
  message(FATAL_ERROR "CNet observer declaration is missing")
endif()
if(NOT _observer_owned_index EQUAL -1)
  message(FATAL_ERROR
    "Owned receive must remain additive and must not widen cnet_observer ABI")
endif()

message(STATUS
  "CNet owned receive contract verified: borrowed ABI unchanged, explicit global-pool owner materialization")
