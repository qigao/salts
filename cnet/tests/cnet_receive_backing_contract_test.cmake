cmake_minimum_required(VERSION 3.25)

file(READ "${PROJECT_SOURCE_DIR}/cnet/src/cnet_owner.c" _owner)
file(READ "${PROJECT_SOURCE_DIR}/cnet/src/cnet_event.c" _event)
file(READ "${PROJECT_SOURCE_DIR}/cnet/src/cnet_dispatcher.c" _dispatcher)
file(READ "${PROJECT_SOURCE_DIR}/cnet/src/cnet_client.c" _client)

foreach(_required IN ITEMS
    "mem_buffer_t *receive_buffer;"
    "mem_buffer_t *receive_spare;"
    "mem_buffer_ref_count(session->receive_spare) == UINT32_C(1)"
    "session->receive_spare = published;"
    ".buffer = mem_buffer_data(session->receive_buffer)"
    "mem_set_used(session->receive_buffer, completion->bytes);"
    "completion->bytes != 0u ? session->receive_buffer : NULL"
    "mem_buffer_retain(event->backing)"
    "entry->canonical_backing = data_event && event->backing != NULL;"
    "out_view->backing = entry->canonical_backing ? entry->payload : NULL;"
    "job->event.backing"
    "view->backing != NULL"
    "mem_slice(view->backing, 0u, view->size)")
  string(FIND "${_owner}\n${_event}\n${_dispatcher}\n${_client}"
              "${_required}" _index)
  if(_index EQUAL -1)
    message(FATAL_ERROR
      "CNet producer-owned receive contract is missing required fragment: ${_required}")
  endif()
endforeach()

foreach(_forbidden IN ITEMS
    "session->receive_buffer = (unsigned char *)malloc"
    ".buffer = session->receive_buffer,")
  string(FIND "${_owner}" "${_forbidden}" _index)
  if(NOT _index EQUAL -1)
    message(FATAL_ERROR
      "CNet receive owner must not fall back to raw scratch storage: ${_forbidden}")
  endif()
endforeach()

message(STATUS
  "CNet producer-owned receive contract verified: canonical backing reaches dispatcher/client without DATA memcpy")
