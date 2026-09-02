foreach(required_var
        SOURCE_DIR
        BUILD_DIR
        CMAKE_COMMAND_PATH
        BUILD_CONFIG
        BUILD_GENERATOR
        DEPENDENCY_PREFIX_PATH)
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
set(consumer_prefix_path "${install_prefix}")
list(APPEND consumer_prefix_path ${DEPENDENCY_PREFIX_PATH})
file(REMOVE_RECURSE "${smoke_root}")

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}" --install "${build_root}"
          --prefix "${install_prefix}" --config "${BUILD_CONFIG}"
  RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "Rocida package install failed: ${install_result}")
endif()

foreach(required_package_file IN ITEMS RocidaConfig.cmake RocidaTargets.cmake)
  if(NOT EXISTS
     "${install_prefix}/lib/cmake/Rocida/${required_package_file}")
    message(FATAL_ERROR
            "Rocida install is missing ${required_package_file}")
  endif()
endforeach()
if(EXISTS "${install_prefix}/lib/cmake/TurboUtils")
  message(FATAL_ERROR "Rocida install unexpectedly contains TurboUtils package files")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}"
          -S "${SOURCE_DIR}/tests/install_consumer"
          -B "${consumer_build}"
          -G "${BUILD_GENERATOR}"
          "-DCMAKE_BUILD_TYPE=${BUILD_CONFIG}"
          "-DCMAKE_PREFIX_PATH=${consumer_prefix_path}"
          "-DRocida_DIR=${install_prefix}/lib/cmake/Rocida"
  RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR
          "Rocida installed consumer configure failed: ${configure_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}" --build "${consumer_build}"
          --config "${BUILD_CONFIG}"
  RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR
          "Rocida installed consumer build failed: ${build_result}")
endif()
