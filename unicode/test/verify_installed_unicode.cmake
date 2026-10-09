# Standalone installed Unicode SDK qualification for native CI profiles.
# No source-tree includes, Unicode table paths, or SaltsUtils exports are used.
foreach(required IN ITEMS SALTS_BINARY_DIR CONSUMER_SOURCE_DIR WORK_DIR
                         CTEST_COMMAND SALTS_BUILD_GENERATOR SALTS_TEST_CONFIG
                         CONSUMER_CACHE)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "installed Unicode SDK check: missing ${required}")
  endif()
endforeach()

# The installed tests need a fresh prefix to detect missing installed files.
# Resolve the destination before cleanup and admit only this test's directory.
file(REAL_PATH "${SALTS_BINARY_DIR}" _binary_root)
file(REAL_PATH "${WORK_DIR}" _work_root)
if(NOT SALTS_TEST_CONFIG MATCHES "^[A-Za-z0-9_+-]+$" OR
   NOT _work_root STREQUAL "${_binary_root}/unicode/test/installed-unicode/${SALTS_TEST_CONFIG}")
  message(FATAL_ERROR "Installed Unicode cleanup must stay in its configuration-specific test directory")
endif()
set(prefix "${_work_root}/prefix")
set(build "${_work_root}/build")
file(REMOVE_RECURSE "${_work_root}")
file(MAKE_DIRECTORY "${WORK_DIR}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${SALTS_BINARY_DIR}"
    --config "${SALTS_TEST_CONFIG}" --prefix "${prefix}"
  RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "Salts Unicode SDK installation failed: ${install_result}")
endif()

set(ENV{SALTS_ROOT} "${prefix}")
set(ENV{SALTS_UNICODE_TEST_BUILD_DIR} "${build}")

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    --preset installed-sdk --fresh -C "${CONSUMER_CACHE}"
    -G "${SALTS_BUILD_GENERATOR}"
  WORKING_DIRECTORY "${CONSUMER_SOURCE_DIR}"
  RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "Installed Unicode C11/C++17 configure failed: ${configure_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build --preset installed-sdk
    --config "${SALTS_TEST_CONFIG}" --parallel 2
  WORKING_DIRECTORY "${CONSUMER_SOURCE_DIR}"
  RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Installed Unicode C11/C++17 build failed: ${build_result}")
endif()

# Installed Core is a shared library; consumers resolve it from this prefix.
if(WIN32)
  set(ENV{PATH} "${prefix}/bin;$ENV{PATH}")
elseif(APPLE)
  set(ENV{DYLD_LIBRARY_PATH} "${prefix}/lib:$ENV{DYLD_LIBRARY_PATH}")
else()
  set(ENV{LD_LIBRARY_PATH} "${prefix}/lib:$ENV{LD_LIBRARY_PATH}")
endif()

execute_process(
  COMMAND "${CTEST_COMMAND}" --preset installed-sdk -C "${SALTS_TEST_CONFIG}"
  WORKING_DIRECTORY "${CONSUMER_SOURCE_DIR}"
  RESULT_VARIABLE test_result)
if(NOT test_result EQUAL 0)
  message(FATAL_ERROR "Installed Unicode C11/C++17 runtime failed: ${test_result}")
endif()
