if(NOT DEFINED PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(client_source "${PROJECT_SOURCE_DIR}/cnet/src/cnet_client.c")
file(READ "${client_source}" client)

string(FIND "${client}" "int cnet_client_poll(" poll_marker)
if(poll_marker EQUAL -1)
  message(FATAL_ERROR "CNet client poll marker missing")
endif()

# External-progress functions are serialized control-plane entry points. They
# intentionally use control_lock and must not weaken the existing invariant
# that send/receive/close hot paths before them remain owner-local and lock-free.
string(FIND "${client}" "static int cnet_client_external_progress_begin(" external_marker)
if(external_marker EQUAL -1)
  set(data_path_end ${poll_marker})
else()
  set(data_path_end ${external_marker})
endif()

string(SUBSTRING "${client}" 0 ${data_path_end} data_path)
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
    "cnet_shards_send_buffer_direct("
    "cnet_shards_send_slice_direct("
    "cnet_shards_send_slicev_direct("
    "cnet_shards_receive_direct("
    "cnet_shards_close_direct(")
  string(FIND "${data_path}" "${direct_marker}" direct_path)
  if(direct_path EQUAL -1)
    message(FATAL_ERROR "CNet owner-local direct path missing: ${direct_marker}")
  endif()
endforeach()

string(FIND "${data_path}" "if (cnet_active_callback_client == impl)" callback_marker)
if(callback_marker EQUAL -1)
  message(FATAL_ERROR "CNet callback reentrancy routing marker missing")
endif()


set(owner_source "${PROJECT_SOURCE_DIR}/cnet/src/cnet_owner.c")
file(READ "${owner_source}" owner)

string(FIND "${owner}" "int cnet_owner_send_buffer_direct(" tls_buffer_marker)
string(FIND "${owner}" "int cnet_owner_send_slice_direct(" tls_buffer_end)
if(tls_buffer_marker EQUAL -1 OR tls_buffer_end EQUAL -1 OR tls_buffer_end LESS tls_buffer_marker)
  message(FATAL_ERROR "CNet retained-buffer direct owner path markers missing")
endif()
math(EXPR tls_buffer_length "${tls_buffer_end} - ${tls_buffer_marker}")
string(SUBSTRING "${owner}" ${tls_buffer_marker} ${tls_buffer_length} tls_buffer_path)
string(FIND "${tls_buffer_path}" "cnet_write_queue_enqueue_buffer" tls_buffer_enqueue)
if(tls_buffer_enqueue EQUAL -1)
  message(FATAL_ERROR "CNet retained-buffer direct path no longer uses owner-local write FIFO")
endif()
string(FIND "${tls_buffer_path}" "CNET_URI_TLS" tls_buffer_tls_reject)
if(NOT tls_buffer_tls_reject EQUAL -1)
  message(FATAL_ERROR "CNet retained-buffer direct path unexpectedly rejects TLS")
endif()

string(FIND "${owner}" "int cnet_owner_send_slicev_direct(" tls_sg_marker)
string(FIND "${owner}" "int cnet_owner_send_slicev_close_direct(" tls_sg_end)
if(tls_sg_marker EQUAL -1 OR tls_sg_end EQUAL -1 OR tls_sg_end LESS tls_sg_marker)
  message(FATAL_ERROR "CNet retained-SG direct owner path markers missing")
endif()
math(EXPR tls_sg_length "${tls_sg_end} - ${tls_sg_marker}")
string(SUBSTRING "${owner}" ${tls_sg_marker} ${tls_sg_length} tls_sg_path)
string(FIND "${tls_sg_path}" "session->peer.scheme == CNET_URI_TLS" tls_sg_tls_reject)
if(NOT tls_sg_tls_reject EQUAL -1)
  message(FATAL_ERROR "CNet retained-SG direct path unexpectedly rejects TLS")
endif()
string(FIND "${tls_sg_path}" "session->peer.scheme == CNET_URI_UDP" tls_sg_udp_reject)
if(tls_sg_udp_reject EQUAL -1)
  message(FATAL_ERROR "CNet retained-SG direct path must still reject UDP")
endif()


string(FIND "${client}" "native_io_backend_spawn_coroutine(" client_coroutine_spawn)
if(NOT client_coroutine_spawn EQUAL -1)
  message(FATAL_ERROR "CNet client introduced a private NativeIO coroutine owner path")
endif()
string(FIND "${owner}" "native_io_backend_spawn_coroutine(" owner_coroutine_spawn)
if(NOT owner_coroutine_spawn EQUAL -1)
  message(FATAL_ERROR "CNet owner introduced NativeIO coroutine frames into the session kernel")
endif()
string(FIND "${client}" "salts_coro_executor_t" client_executor)
string(FIND "${owner}" "salts_coro_executor_t" owner_executor)
if(NOT client_executor EQUAL -1 OR NOT owner_executor EQUAL -1)
  message(FATAL_ERROR "CNet introduced a private generic Coroutine Executor")
endif()

message(STATUS "CNet client single-owner lock/direct-path contract passed")
