# Standalone installed Unicode SDK qualification for native CI profiles.
# No source-tree includes, Unicode table paths, or SaltsUtils exports are used.
foreach(required IN ITEMS SALTS_BINARY_DIR CONSUMER_SOURCE_DIR WORK_DIR
                         CTEST_COMMAND SALTS_BUILD_GENERATOR)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "installed Unicode SDK check: missing ${required}")
  endif()
endforeach()

set(prefix "${WORK_DIR}/prefix")
set(build "${WORK_DIR}/build")
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${SALTS_BINARY_DIR}" --prefix "${prefix}"
  RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "Salts Unicode SDK installation failed: ${install_result}")
endif()

set(_header "${prefix}/include/unicode/salts_unicode.h")
set(_targets "${prefix}/lib/cmake/Salts/SaltsTargets.cmake")
if(NOT EXISTS "${_header}" OR NOT EXISTS "${_targets}")
  message(FATAL_ERROR "Installed Salts Unicode SDK header or target export missing")
endif()
file(READ "${_targets}" _export_contents)
if(NOT _export_contents MATCHES "Salts::Unicode")
  message(FATAL_ERROR "Installed Salts targets do not export Salts::Unicode")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${CONSUMER_SOURCE_DIR}"
    -B "${build}"
    -G "${SALTS_BUILD_GENERATOR}"
    "-DCMAKE_PREFIX_PATH=${prefix}"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY=FALSE
    -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=FALSE
  RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "Installed Unicode C11/C++17 configure failed: ${configure_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${build}" --parallel 2
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
  COMMAND "${CTEST_COMMAND}" --test-dir "${build}" --output-on-failure
  RESULT_VARIABLE test_result)
if(NOT test_result EQUAL 0)
  message(FATAL_ERROR "Installed Unicode C11/C++17 runtime failed: ${test_result}")
endif()
