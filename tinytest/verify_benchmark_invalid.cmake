if(NOT DEFINED TEST_EXE)
  message(FATAL_ERROR "TEST_EXE is required")
endif()
if(NOT DEFINED EXPECTED_DETAIL)
  set(EXPECTED_DETAIL "requires at least one sample")
endif()

execute_process(
  COMMAND "${TEST_EXE}"
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)

if(NOT rc EQUAL 1)
  message(FATAL_ERROR "invalid benchmark run should fail with rc=1, got ${rc}\nstdout:\n${out}\nstderr:\n${err}")
endif()

string(REPLACE "\r" "" out "${out}")
if(NOT out MATCHES "${EXPECTED_DETAIL}")
  message(FATAL_ERROR "benchmark failure message missing:\n${out}")
endif()
