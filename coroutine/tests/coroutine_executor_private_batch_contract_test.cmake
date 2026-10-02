if(NOT DEFINED PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(public_header
    "${PROJECT_SOURCE_DIR}/coroutine/include/salts_coro_executor.h")
set(internal_header
    "${PROJECT_SOURCE_DIR}/coroutine/src/salts_coro_executor_internal.h")
set(coroutine_cmake
    "${PROJECT_SOURCE_DIR}/coroutine/CMakeLists.txt")

file(READ "${public_header}" public_api)
file(READ "${internal_header}" internal_api)
file(READ "${coroutine_cmake}" coroutine_build)

function(require_marker text marker label)
  string(FIND "${text}" "${marker}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "${label}: required marker missing: ${marker}")
  endif()
endfunction()

function(forbid_marker text marker label)
  string(FIND "${text}" "${marker}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "${label}: forbidden marker present: ${marker}")
  endif()
endfunction()

foreach(marker
    "salts_coro_executor_try_submit_batch_to_internal"
    "salts_coro_executor_set_dequeue_batch_limit_internal"
    "SALTS_CORO_EXECUTOR_INTERNAL_MAX_DEQUEUE_BATCH")
  require_marker("${internal_api}" "${marker}"
                 "Coroutine private batch POC")
  forbid_marker("${public_api}" "${marker}"
                "Coroutine public API")
endforeach()

# Do not allow a public spelling to appear while #668 keeps productization
# gated on a real range consumer and paired end-to-end evidence.
foreach(marker
    "salts_coro_executor_submit_batch"
    "salts_coro_executor_try_submit_batch"
    "salts_coro_executor_set_dequeue_batch")
  forbid_marker("${public_api}" "${marker}"
                "Coroutine public batch boundary")
endforeach()

# The private hook lives under src/, while the installed header tree is include/.
require_marker("${coroutine_build}" "DIRECTORY include/"
               "Coroutine installed-header boundary")
forbid_marker("${coroutine_build}" "salts_coro_executor_internal.h"
              "Coroutine private header installation boundary")

# Production semantic consumers may use only the installed/public Coroutine
# contract. Tests and benchmarks are intentionally excluded from this scan.
file(GLOB_RECURSE production_sources
     LIST_DIRECTORIES false
     "${PROJECT_SOURCE_DIR}/native-io/src/*.c"
     "${PROJECT_SOURCE_DIR}/native-io/src/*.h"
     "${PROJECT_SOURCE_DIR}/cnet/src/*.c"
     "${PROJECT_SOURCE_DIR}/cnet/src/*.h"
     "${PROJECT_SOURCE_DIR}/cflow/src/*.c"
     "${PROJECT_SOURCE_DIR}/cflow/src/*.h"
     "${PROJECT_SOURCE_DIR}/cflow/cnet-adapter/*.c"
     "${PROJECT_SOURCE_DIR}/cflow/cnet-adapter/*.h")

foreach(source IN LISTS production_sources)
  file(READ "${source}" content)
  foreach(marker
      "salts_coro_executor_internal.h"
      "salts_coro_executor_try_submit_batch_to_internal"
      "salts_coro_executor_set_dequeue_batch_limit_internal"
      "SALTS_CORO_EXECUTOR_INTERNAL_MAX_DEQUEUE_BATCH")
    forbid_marker("${content}" "${marker}"
                  "Production private-batch boundary in ${source}")
  endforeach()
endforeach()

message(STATUS "Coroutine private batch boundary contract passed")
