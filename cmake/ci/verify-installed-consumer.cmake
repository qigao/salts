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

set(configure_args
  -S "${CONSUMER_SOURCE_DIR}" -B "${build}"
  "-DCMAKE_PREFIX_PATH=${prefix}"
  "-DSALTS_TEST_PREFIX=${prefix}"
  "-DSALTS_TEST_VERSION=${SALTS_TEST_VERSION}"
  -DCMAKE_FIND_USE_PACKAGE_REGISTRY=FALSE
  -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=FALSE)
if(DEFINED SALTS_GENERATOR AND NOT SALTS_GENERATOR STREQUAL "")
  list(APPEND configure_args -G "${SALTS_GENERATOR}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" ${configure_args}
  RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "installed Salts SDK consumer configure failed: ${configure_result}")
endif()

set(build_args --build "${build}")
set(test_args --test-dir "${build}" --output-on-failure)
if(DEFINED SALTS_TEST_CONFIG AND NOT SALTS_TEST_CONFIG STREQUAL "")
  list(APPEND build_args --config "${SALTS_TEST_CONFIG}")
  list(APPEND test_args -C "${SALTS_TEST_CONFIG}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" ${build_args}
  RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "installed Salts SDK consumer build failed: ${build_result}")
endif()

# A Windows consumer must load the exact newly installed DLLs, not a
# developer's default PATH or stale build-tree/native SDK dependency.
if(WIN32)
  set(ENV{PATH} "${prefix}/bin;$ENV{PATH}")
endif()
execute_process(COMMAND "${CTEST_COMMAND}" ${test_args}
  RESULT_VARIABLE test_result)
if(NOT test_result EQUAL 0)
  message(FATAL_ERROR "installed Salts SDK consumer test failed: ${test_result}")
endif()
