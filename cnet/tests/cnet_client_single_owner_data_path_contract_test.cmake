if(NOT DEFINED PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(client_source "${PROJECT_SOURCE_DIR}/cnet/src/cnet_client.c")
file(READ "${client_source}" client)

string(FIND "${client}" "int cnet_client_poll(" poll_marker)
if(poll_marker EQUAL -1)
  message(FATAL_ERROR "CNet client poll marker missing")
endif()

string(SUBSTRING "${client}" 0 ${poll_marker} data_path)
string(FIND "${data_path}" "salts_mutex_lock(&impl->control_lock)" data_lock)
if(NOT data_lock EQUAL -1)
  message(FATAL_ERROR "CNet single-owner data path reacquired the control mutex")
endif()
string(FIND "${data_path}" "salts_mutex_unlock(&impl->control_lock)" data_unlock)
if(NOT data_unlock EQUAL -1)
  message(FATAL_ERROR "CNet single-owner data path contains control mutex unlock")
endif()

string(FIND "${client}" "salts_mutex_t control_lock;" control_type)
if(control_type EQUAL -1)
  message(FATAL_ERROR "CNet control-only mutex marker missing")
endif()
string(FIND "${client}" "int cnet_client_wake(" wake_marker)
if(wake_marker EQUAL -1)
  message(FATAL_ERROR "CNet concurrent wake contract marker missing")
endif()

foreach(direct_marker
    "cnet_shards_send_buffer_direct(&impl->shards"
    "cnet_shards_send_slice_direct(&impl->shards"
    "cnet_shards_send_slicev_direct(&impl->shards"
    "cnet_shards_receive_direct(&impl->shards"
    "cnet_shards_close_direct(&impl->shards")
  string(FIND "${data_path}" "${direct_marker}" direct_path)
  if(direct_path EQUAL -1)
    message(FATAL_ERROR "CNet owner-local direct path missing: ${direct_marker}")
  endif()
endforeach()

string(FIND "${data_path}" "if (cnet_active_callback_client == impl)" callback_marker)
if(callback_marker EQUAL -1)
  message(FATAL_ERROR "CNet callback reentrancy routing marker missing")
endif()

message(STATUS "CNet client single-owner lock/direct-path contract passed")
