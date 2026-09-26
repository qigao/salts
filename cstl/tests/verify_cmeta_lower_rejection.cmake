if(NOT DEFINED TOOL OR NOT EXISTS "${TOOL}")
  message(FATAL_ERROR "cmeta-lower tool is missing")
endif()
if(NOT DEFINED INPUT OR NOT EXISTS "${INPUT}")
  message(FATAL_ERROR "rejection fixture is missing")
endif()
if(NOT DEFINED OUTPUT)
  message(FATAL_ERROR "rejection output path is missing")
endif()
if(NOT DEFINED EXPECTED)
  message(FATAL_ERROR "expected diagnostic is missing")
endif()

execute_process(
  COMMAND "${TOOL}" "${INPUT}" "${OUTPUT}"
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr)

if(rc EQUAL 0)
  message(FATAL_ERROR "cmeta-lower unexpectedly accepted invalid input")
endif()

string(FIND "${stderr}" "${EXPECTED}" found)
if(found EQUAL -1)
  message(FATAL_ERROR
          "expected diagnostic not found: ${EXPECTED}\nstderr:\n${stderr}")
endif()

if(EXISTS "${OUTPUT}")
  file(REMOVE "${OUTPUT}")
endif()
