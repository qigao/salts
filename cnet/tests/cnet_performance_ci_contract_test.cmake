if(NOT DEFINED PROJECT_SOURCE_DIR)
  message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(workflow "${PROJECT_SOURCE_DIR}/.github/workflows/native-io-release-benchmarks.yml")
set(scaling_verifier "${PROJECT_SOURCE_DIR}/cnet/benchmarks/verify_scaling_benchmark.ps1")
set(io_verifier "${PROJECT_SOURCE_DIR}/cnet/benchmarks/verify_io_benchmark.ps1")

file(READ "${workflow}" workflow_text)
file(READ "${scaling_verifier}" scaling_text)
file(READ "${io_verifier}" io_text)

function(require_marker text marker label)
  string(FIND "${text}" "${marker}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "${label}: required marker missing: ${marker}")
  endif()
endfunction()

function(forbid_marker text marker label)
  string(FIND "${text}" "${marker}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "${label}: copied-send CI marker must stay retired: ${marker}")
  endif()
endfunction()

require_marker("${workflow_text}"
               "Run libuv versus NativeIO direct/coroutine versus CNet retained benchmark"
               "CNet canonical benchmark workflow")
require_marker("${workflow_text}"
               "Run NativeIO direct versus CNet retained scaling benchmark"
               "CNet canonical scaling workflow")
require_marker("${scaling_text}"
               "$drivers = @(\"NativeIO direct\", \"CNet retained\")"
               "CNet retained-only scaling verifier")

require_marker("${scaling_text}"
               "UNSTABLE retained scaling cell"
               "CNet scaling measurement-validity gate")
require_marker("${scaling_text}"
               "no performance verdict"
               "CNet unstable-cell neutral verdict")
require_marker("${scaling_text}"
               "Where-Object { $_.Mad -gt $_.Limit }"
               "CNet noise gate before performance verdict")


foreach(marker
    "CNet copy"
    "copy/retained"
    "CNET_IO_BENCHMARK_SEND_COMPARE"
    "-SendComparison")
  forbid_marker("${workflow_text}" "${marker}" "CNet benchmark workflow")
  forbid_marker("${scaling_text}" "${marker}" "CNet scaling verifier")
  forbid_marker("${io_text}" "${marker}" "CNet I/O verifier")
endforeach()

message(STATUS "CNet retained-only performance CI contract passed")
