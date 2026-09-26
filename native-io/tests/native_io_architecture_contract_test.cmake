if(NOT DEFINED PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(native_io_header "${PROJECT_SOURCE_DIR}/native-io/include/salts/native_io.h")
set(native_ipc_header "${PROJECT_SOURCE_DIR}/native-io/include/salts/native_ipc.h")
set(native_io_cmake "${PROJECT_SOURCE_DIR}/native-io/CMakeLists.txt")

file(READ "${native_io_header}" native_io)
file(READ "${native_ipc_header}" native_ipc)
file(READ "${native_io_cmake}" native_io_build)

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
