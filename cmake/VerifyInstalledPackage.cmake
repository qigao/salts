foreach(required_var
        SOURCE_DIR
        BUILD_DIR
        CMAKE_COMMAND_PATH
        BUILD_CONFIG
        BUILD_GENERATOR)
  if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
    message(FATAL_ERROR "VerifyInstalledPackage requires ${required_var}")
  endif()
endforeach()

cmake_path(ABSOLUTE_PATH BUILD_DIR NORMALIZE OUTPUT_VARIABLE build_root)
set(smoke_root "${build_root}/package-smoke")
cmake_path(IS_PREFIX build_root "${smoke_root}" NORMALIZE smoke_is_in_build)
if(NOT smoke_is_in_build)
  message(FATAL_ERROR "package smoke directory escaped the build tree")
endif()

set(install_prefix "${smoke_root}/install")
set(consumer_build "${smoke_root}/consumer")
file(REMOVE_RECURSE "${smoke_root}")

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}" --install "${build_root}"
          --prefix "${install_prefix}" --config "${BUILD_CONFIG}"
  RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "TurboUtils package install failed: ${install_result}")
endif()

if(EXPECT_CAPTURE_WINDOWS_RUNTIME)
  foreach(capture_runtime_dependency libyuv.dll jpeg62.dll)
    set(capture_runtime_path
        "${install_prefix}/bin/${capture_runtime_dependency}")
    if(NOT EXISTS "${capture_runtime_path}")
      message(FATAL_ERROR
              "TurboUtils Capture package is missing ${capture_runtime_path}")
    endif()
  endforeach()
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}"
          -S "${SOURCE_DIR}/tests/install_consumer"
          -B "${consumer_build}"
          -G "${BUILD_GENERATOR}"
          "-DCMAKE_BUILD_TYPE=${BUILD_CONFIG}"
          "-DCMAKE_PREFIX_PATH=${install_prefix}"
          "-DTurboUtils_DIR=${install_prefix}/lib/cmake/TurboUtils"
          "-DTURBOUTILS_EXPECT_CFLOW_MINICORO=${EXPECT_CFLOW_MINICORO}"
          "-DTURBOUTILS_EXPECT_CFLOW_USB=${EXPECT_CFLOW_USB}"
          "-DTURBOUTILS_EXPECT_CAPTURE=${EXPECT_CAPTURE}"
  RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR
          "TurboUtils installed consumer configure failed: ${configure_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}" --build "${consumer_build}"
          --config "${BUILD_CONFIG}"
  RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR
          "TurboUtils installed consumer build failed: ${build_result}")
endif()

if(EXPECT_CAPTURE_WINDOWS_RUNTIME)
  set(capture_consumer "${consumer_build}/consume_capture.exe")
  if(NOT EXISTS "${capture_consumer}")
    set(capture_consumer
        "${consumer_build}/${BUILD_CONFIG}/consume_capture.exe")
  endif()
  if(NOT EXISTS "${capture_consumer}")
    message(FATAL_ERROR
            "TurboUtils Capture consumer executable was not generated")
  endif()

  set(capture_runtime_path
      "${install_prefix}/bin;$ENV{SystemRoot}/System32;$ENV{SystemRoot}")
  execute_process(
    COMMAND "${CMAKE_COMMAND_PATH}" -E env "PATH=${capture_runtime_path}"
            "${capture_consumer}"
    WORKING_DIRECTORY "${install_prefix}/bin"
    RESULT_VARIABLE capture_consumer_result)
  if(NOT capture_consumer_result EQUAL 0)
    message(FATAL_ERROR
            "TurboUtils installed Capture consumer failed: ${capture_consumer_result}")
  endif()
endif()
