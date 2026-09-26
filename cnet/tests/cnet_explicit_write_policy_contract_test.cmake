if(NOT DEFINED PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

file(READ "${PROJECT_SOURCE_DIR}/cnet/src/cnet_client.c" client)
file(READ "${PROJECT_SOURCE_DIR}/cnet/src/cnet_shards.c" shards)
file(READ "${PROJECT_SOURCE_DIR}/cnet/src/cnet_write_queue.c" writes)

foreach(marker
    "config->write_capacity == 0u"
    "config->write_capacity_per_connection == 0u"
    "config->write_buffer_bytes < config->max_send_bytes")
  string(FIND "${client}" "${marker}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "explicit client write policy marker missing: ${marker}")
  endif()
endforeach()

foreach(forbidden
    ".write_capacity_per_shard = config->command_capacity"
    "config->command_capacity * config->max_send_bytes")
  string(FIND "${client}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR "steady-state write policy fell back to command policy: ${forbidden}")
  endif()
endforeach()

foreach(forbidden
    "config->write_capacity_per_shard != 0u ?"
    "config->max_write_payload_bytes != 0u ?")
  string(FIND "${shards}" "${forbidden}" found)
  if(NOT found EQUAL -1)
    message(FATAL_ERROR "shards write policy fallback returned: ${forbidden}")
  endif()
endforeach()

string(FIND "${writes}" "impl->counts[connection_index] >= impl->per_connection_capacity"
       per_connection_gate)
if(per_connection_gate EQUAL -1)
  message(FATAL_ERROR "per-connection write admission cap is not enforced")
endif()

message(STATUS "CNet explicit write admission policy contract passed")
