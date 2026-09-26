if(NOT DEFINED PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

file(READ "${PROJECT_SOURCE_DIR}/cflow/CMakeLists.txt" core_cmake)
file(READ "${PROJECT_SOURCE_DIR}/cflow/include/cflow/cflow.h" aggregate_header)
file(READ "${PROJECT_SOURCE_DIR}/cflow/cnet-adapter/CMakeLists.txt" adapter_cmake)
file(READ "${PROJECT_SOURCE_DIR}/cflow/cnet-adapter/io_cnet_adapter.c" adapter_source)

string(FIND "${core_cmake}" "Salts::CNet" core_cnet)
if(NOT core_cnet EQUAL -1)
  message(FATAL_ERROR "Salts::CFlow core must not depend on Salts::CNet")
endif()

string(FIND "${aggregate_header}" "io_cnet_adapter.h" aggregate_cnet)
if(NOT aggregate_cnet EQUAL -1)
  message(FATAL_ERROR "cflow/cflow.h must not make the optional CNet adapter implicit")
endif()

foreach(marker
    "ALIAS Salts::CFlowCNet"
    "PUBLIC Salts::CFlow Salts::CNet")
  string(FIND "${adapter_cmake}" "${marker}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "CFlow CNet adapter target marker missing: ${marker}")
  endif()
endforeach()


string(FIND "${adapter_source}" "cnet_receive(" receive_call)
if(receive_call EQUAL -1)
  message(FATAL_ERROR "CFlow CNet adapter must map demand through cnet_receive")
endif()

foreach(marker
    "cnet_client_poll("
    "cnet_close("
    "cnet_client_stop("
    "cnet_client_destroy("
    "native_io_"
    "threadpool")
  string(FIND "${adapter_source}" "${marker}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR
      "CFlow CNet adapter must not own CNet progress/session teardown/NativeIO/runtime: ${marker}")
  endif()
endforeach()

message(STATUS "CFlow/CNet optional dependency boundary passed")
