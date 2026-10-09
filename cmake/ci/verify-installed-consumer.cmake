if(NOT DEFINED SALTS_BINARY_DIR OR
   NOT DEFINED SALTS_TEST_VERSION OR
   NOT DEFINED CONSUMER_SOURCE_DIR OR
   NOT DEFINED WORK_DIR OR
   NOT DEFINED CTEST_COMMAND)
  message(FATAL_ERROR "installed consumer test arguments are required")
endif()

set(prefix "${WORK_DIR}/prefix")
set(build "${WORK_DIR}/build")

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${SALTS_BINARY_DIR}" --prefix "${prefix}"
  RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "Salts SDK install failed: ${install_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${CONSUMER_SOURCE_DIR}"
    -B "${build}"
    "-DCMAKE_PREFIX_PATH=${prefix}"
    "-DSALTS_TEST_PREFIX=${prefix}"
    "-DSALTS_TEST_VERSION=${SALTS_TEST_VERSION}"
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY=FALSE
    -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=FALSE
  RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "installed Salts SDK consumer configure failed: ${configure_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${build}"
  RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "installed Salts SDK consumer build failed: ${build_result}")
endif()

execute_process(
  COMMAND "${CTEST_COMMAND}" --test-dir "${build}" --output-on-failure
  RESULT_VARIABLE test_result)
if(NOT test_result EQUAL 0)
  message(FATAL_ERROR "installed Salts SDK consumer test failed: ${test_result}")
endif()
