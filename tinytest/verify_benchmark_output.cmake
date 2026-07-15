if(NOT DEFINED TEST_EXE)
  message(FATAL_ERROR "TEST_EXE is required")
endif()

execute_process(
  COMMAND "${TEST_EXE}" --no-color
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)

if(NOT rc EQUAL 0)
  message(FATAL_ERROR "benchmark output run failed with rc=${rc}\nstdout:\n${out}\nstderr:\n${err}")
endif()

string(REPLACE "\r" "" out "${out}")

if(PLAIN_OUTPUT)
  foreach(fragment
      "batch[ ]+samples=2[ ]+ops/sample=1[ ]+bytes/sample=-"
      "operations[ ]+samples=2[ ]+ops/sample=4[ ]+bytes/sample=-"
      "bytes[ ]+samples=2[ ]+ops/sample=1[ ]+bytes/sample=16"
      "io[ ]+samples=2[ ]+ops/sample=4[ ]+bytes/sample=16")
    if(NOT out MATCHES "${fragment}")
      message(FATAL_ERROR "plain benchmark output is missing '${fragment}':\n${out}")
    endif()
  endforeach()
  return()
endif()

foreach(label "samples" "ops/sample" "bytes/sample" "avg/op" "min/sample" "max/sample" "ops/s" "MiB/s")
  if(NOT out MATCHES "${label}")
    message(FATAL_ERROR "benchmark output is missing '${label}':\n${out}")
  endif()
endforeach()

foreach(case_name "batch" "operations" "bytes" "io" "legacy alias")
  if(NOT out MATCHES "${case_name}")
    message(FATAL_ERROR "benchmark output is missing '${case_name}':\n${out}")
  endif()
endforeach()

if(NOT out MATCHES "batch[ ]+2[ ]+1[ ]+-")
  message(FATAL_ERROR "batch row does not expose sample/operation units:\n${out}")
endif()
if(NOT out MATCHES "operations[ ]+2[ ]+4[ ]+-")
  message(FATAL_ERROR "operations row does not expose operations per sample:\n${out}")
endif()
if(NOT out MATCHES "bytes[ ]+2[ ]+1[ ]+16")
  message(FATAL_ERROR "bytes row does not expose bytes per sample:\n${out}")
endif()
if(NOT out MATCHES "io[ ]+2[ ]+4[ ]+16")
  message(FATAL_ERROR "io row does not expose both work units:\n${out}")
endif()
