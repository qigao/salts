if(NOT DEFINED TOOL OR NOT EXISTS "${TOOL}")
  message(FATAL_ERROR "inspection codegen tool is missing")
endif()
if(NOT DEFINED OUTPUT)
  message(FATAL_ERROR "rejection output path is missing")
endif()

file(REMOVE "${OUTPUT}")
execute_process(
  COMMAND "${TOOL}" --mismatch "${OUTPUT}"
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)

if(rc EQUAL 0)
  message(FATAL_ERROR "inspection codegen unexpectedly admitted mismatched metadata")
endif()
if(EXISTS "${OUTPUT}")
  message(FATAL_ERROR "rejected inspection metadata left generated output behind")
endif()
string(FIND "${stderr}" "inspection metadata rejected as expected" marker)
if(marker EQUAL -1)
  message(FATAL_ERROR
    "inspection codegen failed for an unexpected reason: ${stderr}")
endif()
