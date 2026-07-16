if(NOT DEFINED TEST_EXE)
  message(FATAL_ERROR "TEST_EXE is required")
endif()

execute_process(
  COMMAND "${TEST_EXE}" --filter keep
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)

if(NOT rc EQUAL 0)
  message(FATAL_ERROR "filter console run failed: ${rc}\nstdout:\n${out}\nstderr:\n${err}")
endif()

string(REPLACE "\r" "" out "${out}")

string(FIND "${out}" "explicit skip" explicit_skip_pos)
if(NOT explicit_skip_pos EQUAL -1)
  message(FATAL_ERROR "explicit skip should be hidden from filtered console output:\n${out}")
endif()

string(FIND "${out}" "  keep test\n  [ OK    ]" keep_ok_pos)
if(keep_ok_pos EQUAL -1)
  message(FATAL_ERROR "selected test entry is missing from filtered console output:\n${out}")
endif()

string(FIND "${out}" "drop test" drop_pos)
if(NOT drop_pos EQUAL -1)
  message(FATAL_ERROR "filtered test should be hidden from console output:\n${out}")
endif()

string(FIND "${out}" "Total tests [SKIPPED]:\t1" skipped_summary_pos)
if(skipped_summary_pos EQUAL -1)
  message(FATAL_ERROR "explicit skip summary is incorrect:\n${out}")
endif()

string(FIND "${out}" "Total tests [FILTERED]:\t1" filtered_summary_pos)
if(filtered_summary_pos EQUAL -1)
  message(FATAL_ERROR "filtered summary is incorrect:\n${out}")
endif()
