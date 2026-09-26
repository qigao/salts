if(NOT DEFINED TOOL OR NOT EXISTS "${TOOL}")
  message(FATAL_ERROR "method lowerer tool is missing: ${TOOL}")
endif()
if(NOT DEFINED INPUT OR NOT EXISTS "${INPUT}")
  message(FATAL_ERROR "method lowering rejection fixture is missing: ${INPUT}")
endif()
if(NOT DEFINED OUTPUT)
  message(FATAL_ERROR "method lowering rejection output path is required")
endif()
if(NOT DEFINED EXPECTED)
  message(FATAL_ERROR "expected method lowering diagnostic is required")
endif()

execute_process(
  COMMAND "${TOOL}" "${INPUT}" "${OUTPUT}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)

if(result EQUAL 0)
  message(FATAL_ERROR "invalid method call unexpectedly lowered successfully")
endif()

set(combined "${stdout}
${stderr}")
string(FIND "${combined}" "${EXPECTED}" found)
if(found EQUAL -1)
  message(FATAL_ERROR
          "expected diagnostic not found: ${EXPECTED}
"
          "lowerer output:
${combined}")
endif()
