if(NOT DEFINED PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(legacy_files
  "cflow/include/cflow/io_native.h"
  "cflow/src/io_native.c"
  "cflow/src/io_native_internal.h"
  "cflow/src/io_native_iocp.c"
  "cflow/src/io_native_io_uring.c"
  "cflow/src/io_native_readiness.c"
  "cflow/tests/cflow_io_native_test.c"
  "cflow/tests/cflow_header_cpp_test.cpp")

set(forbidden_markers
  "cflow_io_native_pipe_operation"
  "CFLOW_IO_NATIVE_PIPE_"
  "cflow_io_native_backend_pipe_supported"
  "cflow_io_native_backend_pipe_actor_ops"
  "cflow_io_native_backend_forget_pipe"
  "submit_pipe"
  "forget_pipe")

foreach(path IN LISTS legacy_files)
  file(READ "${PROJECT_SOURCE_DIR}/${path}" text)
  foreach(marker IN LISTS forbidden_markers)
    string(FIND "${text}" "${marker}" position)
    if(NOT position EQUAL -1)
      message(FATAL_ERROR
        "superseded autonomous Pipe marker returned in ${path}: ${marker}")
    endif()
  endforeach()
endforeach()

file(READ
  "${PROJECT_SOURCE_DIR}/cflow/include/cflow/io_native_adapter.h"
  adapter_header)
foreach(marker
    "cflow_io_native_adapter_attach_pipe"
    "cflow_io_native_adapter_release_pipe")
  string(FIND "${adapter_header}" "${marker}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR
      "canonical CFlow NativeIO Pipe adapter marker missing: ${marker}")
  endif()
endforeach()

file(READ
  "${PROJECT_SOURCE_DIR}/cflow/src/io_native_adapter.c"
  adapter_source)
foreach(marker
    "native_io_backend_attach_pipe"
    "native_io_backend_release_pipe")
  string(FIND "${adapter_source}" "${marker}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR
      "canonical root NativeIO Pipe bridge marker missing: ${marker}")
  endif()
endforeach()

message(STATUS
  "CFlow Pipe data-plane contract passed: root NativeIO adapter is canonical")
