if(NOT DEFINED PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(client_source "${PROJECT_SOURCE_DIR}/cnet/src/cnet_client.c")
file(READ "${client_source}" client)

function(require_marker marker)
  string(FIND "${client}" "${marker}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "CNet canonical-state marker missing: ${marker}")
  endif()
endfunction()

function(forbid_marker marker)
  string(FIND "${client}" "${marker}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "CNet duplicated lifecycle mirror returned: ${marker}")
  endif()
endfunction()

forbid_marker("bool connected;")
forbid_marker("record->connected")

require_marker("cnet_client_record_session_state")
require_marker("cnet_shards_state(&impl->shards, record->internal, out_state)")
require_marker("bool close_command_pending;")
require_marker("bool tls_command_pending;")

message(STATUS "CNet canonical session-state authority contract passed")
