if(NOT DEFINED PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(native_io_header "${PROJECT_SOURCE_DIR}/native-io/include/salts/native_io.h")
set(native_ipc_header "${PROJECT_SOURCE_DIR}/native-io/include/salts/native_ipc.h")
set(native_io_cmake "${PROJECT_SOURCE_DIR}/native-io/CMakeLists.txt")
set(native_io_sharded_header "${PROJECT_SOURCE_DIR}/native-io/include/salts/native_io_sharded.h")
set(native_io_sharded_source "${PROJECT_SOURCE_DIR}/native-io/src/native_io_sharded.c")

file(READ "${native_io_header}" native_io)
file(READ "${native_ipc_header}" native_ipc)
file(READ "${native_io_cmake}" native_io_build)
file(READ "${native_io_sharded_header}" native_io_sharded)
file(READ "${native_io_sharded_source}" native_io_sharded_impl)

function(require_marker text marker label)
  string(FIND "${text}" "${marker}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "${label}: required marker missing: ${marker}")
  endif()
endfunction()

function(forbid_marker text marker label)
  string(FIND "${text}" "${marker}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "${label}: forbidden boundary marker present: ${marker}")
  endif()
endfunction()

# NativeIO keeps a compact mechanism vocabulary.
foreach(marker
    "NATIVE_IO_OPERATION_STREAM_RECV"
    "NATIVE_IO_OPERATION_STREAM_SEND"
    "NATIVE_IO_OPERATION_UDP_RECV_FROM"
    "NATIVE_IO_OPERATION_UDP_SEND_TO"
    "NATIVE_IO_OPERATION_PIPE_READ"
    "NATIVE_IO_OPERATION_PIPE_WRITE"
    "native_io_backend_prepare"
    "native_io_backend_observe"
    "native_io_coroutine_await_prepared")
  require_marker("${native_io}" "${marker}" "NativeIO contract")
endforeach()

# Sharded routing reuses the canonical Coroutine Executor lifecycle and keeps
# raw NativeIO endpoint ABI independent of shard topology.
foreach(marker
    "native_io_sharded_submit_to"
    "native_io_sharded_try_submit_to"
    "native_io_sharded_current_shard"
    "native_io_sharded_context_attach_socket"
    "native_io_sharded_context_attach_pipe"
    "native_io_sharded_context_release_socket"
    "native_io_sharded_context_release_pipe"
    "native_io_sharded_context_submit"
    "native_io_sharded_context_submit_owned"
    "native_io_sharded_context_prepare_owned"
    "native_io_sharded_submit_owned"
    "native_io_sharded_try_submit_owned"
    "native_io_sharded_admission_fn"
    "native_io_sharded_ownership"
    "owner_identity")
  require_marker("${native_io_sharded}" "${marker}" "NativeIO sharded routing contract")
endforeach()
require_marker("${native_io_sharded_impl}" "salts_coro_executor_try_submit_to"
               "NativeIO sharded executor reuse")
require_marker("${native_io_sharded_impl}" "owned_routes"
               "NativeIO sharded bounded owned-route storage")
require_marker("${native_io_sharded_impl}" "native_io_sharded_shutdown_probe"
               "NativeIO sharded shutdown ownership probe")
require_marker("${native_io_sharded_impl}" "native_io_sharded_shutdown_drain"
               "NativeIO sharded terminal shutdown drain")
require_marker("${native_io_sharded_impl}" "shutdown_drain_endpoint_count"
               "NativeIO sharded recoverable endpoint quiescence")
require_marker("${native_io_sharded_impl}" "native_io_sharded_finish_fatal_shutdown_attempt"
               "NativeIO sharded fatal shutdown closure")
require_marker("${native_io_sharded_impl}" "request_ownerships"
               "NativeIO sharded request ownership storage")
require_marker("${native_io_sharded_impl}" "ownership_settlements"
               "NativeIO sharded batch ownership detachment")
require_marker("${native_io_sharded_impl}" "observe_active"
               "NativeIO sharded observe reentrancy boundary")
require_marker("${native_io_sharded_impl}" "ownership.finalize"
               "NativeIO sharded request ownership settlement")
forbid_marker("${native_io}" "owner_shard" "raw NativeIO endpoint ABI")
forbid_marker("${native_io_sharded_impl}" "mem_buffer_t"
              "NativeIO sharded ownership boundary")
forbid_marker("${native_io_sharded_impl}" "disruptor_"
              "NativeIO sharded executor reuse")

# VSOCK stays an upper-layer STREAM transport rather than a NativeIO kind.
foreach(marker
    "NATIVE_IO_OPERATION_VSOCK"
    "NATIVE_IO_ENDPOINT_VSOCK"
    "NATIVE_IO_BACKEND_VSOCK")
  forbid_marker("${native_io}" "${marker}" "NativeIO VSOCK boundary")
endforeach()

# NativeIO must not reverse-depend on semantic consumers.
foreach(marker
    "#include <cnet/"
    "#include <cflow/"
    "Salts::CNet"
    "Salts::CFlow")
  forbid_marker("${native_io}" "${marker}" "NativeIO header dependency boundary")
  forbid_marker("${native_ipc}" "${marker}" "NativeIPC header dependency boundary")
  forbid_marker("${native_io_build}" "${marker}" "NativeIO build dependency boundary")
  forbid_marker("${native_io_sharded}" "${marker}" "NativeIO sharded header dependency boundary")
  forbid_marker("${native_io_sharded_impl}" "${marker}" "NativeIO sharded source dependency boundary")
endforeach()

# NativeIPC is rendezvous/control-plane only. Payload I/O belongs to NativeIO.
foreach(marker
    "salts_ipc_named_pipe_connect"
    "salts_ipc_fifo_open"
    "salts_ipc_pipe_server_try_accept"
    "salts_ipc_pipe_server_observe")
  require_marker("${native_ipc}" "${marker}" "NativeIPC control-plane contract")
endforeach()

foreach(marker
    "salts_ipc_pipe_read("
    "salts_ipc_pipe_write("
    "salts_ipc_send("
    "salts_ipc_receive("
    "salts_ipc_recv(")
  forbid_marker("${native_ipc}" "${marker}" "NativeIPC payload-I/O boundary")
endforeach()

message(STATUS "NativeIO architecture source-boundary contract passed")
