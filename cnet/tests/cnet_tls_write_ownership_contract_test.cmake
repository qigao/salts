if(NOT DEFINED PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(owner_source "${PROJECT_SOURCE_DIR}/cnet/src/cnet_owner.c")
file(READ "${owner_source}" owner)

function(require_marker marker)
  string(FIND "${owner}" "${marker}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "CNet TLS write ownership marker missing: ${marker}")
  endif()
endfunction()

function(forbid_marker marker)
  string(FIND "${owner}" "${marker}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "CNet TLS payload ownership returned to command storage: ${marker}")
  endif()
endfunction()

forbid_marker("tls_send_command")
require_marker("cnet_write_view tls_send_write")
require_marker("cnet_write_queue_settle(&impl->writes, &session->tls_send_write)")
require_marker("cnet_write_queue_peek(&impl->writes, session->handle, &session->tls_send_write)")
require_marker("cnet_tls_write(&session->tls, session->tls_send_write.data")
require_marker("cnet_owner_queue_session_work(impl, session->handle)")

message(STATUS "CNet TLS logical write ownership contract passed")
