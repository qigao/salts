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
set(hidden_dependency_consumer_build
    "${smoke_root}/consumer-nodeps")
set(consumer_prefix_path "${install_prefix}")
list(APPEND consumer_prefix_path ${DEPENDENCY_PREFIX_PATH})
file(REMOVE_RECURSE "${smoke_root}")

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}" --install "${build_root}"
          --prefix "${install_prefix}" --config "${BUILD_CONFIG}"
  RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "Salts package install failed: ${install_result}")
endif()

set(expected_chttp_abi_version 2)
set(expected_crpc_abi_version 2)
if(WIN32)
  set(expected_chttp_runtime
      "${install_prefix}/bin/salts_chttp-${expected_chttp_abi_version}.dll")
  set(expected_chttp_import_library
      "${install_prefix}/lib/salts_chttp-${expected_chttp_abi_version}.lib")
  foreach(expected_chttp_file IN ITEMS
          "${expected_chttp_runtime}"
          "${expected_chttp_import_library}")
    if(NOT EXISTS "${expected_chttp_file}")
      message(FATAL_ERROR
              "Salts install is missing CHTTP ABI ${expected_chttp_abi_version} artifact: ${expected_chttp_file}")
    endif()
  endforeach()
  set(expected_crpc_archive
      "${install_prefix}/lib/salts_crpc-${expected_crpc_abi_version}.lib")
elseif(APPLE)
  set(expected_chttp_runtime
      "${install_prefix}/lib/libsalts_chttp.${expected_chttp_abi_version}.dylib")
  if(NOT EXISTS "${expected_chttp_runtime}")
    message(FATAL_ERROR
            "Salts install is missing CHTTP ABI ${expected_chttp_abi_version} artifact: ${expected_chttp_runtime}")
  endif()
  set(expected_crpc_archive
      "${install_prefix}/lib/libsalts_crpc-${expected_crpc_abi_version}.a")
else()
  set(expected_chttp_runtime
      "${install_prefix}/lib/libsalts_chttp.so.${expected_chttp_abi_version}")
  if(NOT EXISTS "${expected_chttp_runtime}")
    message(FATAL_ERROR
            "Salts install is missing CHTTP ABI ${expected_chttp_abi_version} artifact: ${expected_chttp_runtime}")
  endif()
  set(expected_crpc_archive
      "${install_prefix}/lib/libsalts_crpc-${expected_crpc_abi_version}.a")
endif()

if(NOT EXISTS "${expected_crpc_archive}")
  message(FATAL_ERROR
          "Salts install is missing CRPC ABI ${expected_crpc_abi_version} artifact: ${expected_crpc_archive}")
endif()

foreach(required_package_file IN ITEMS SaltsConfig.cmake SaltsTargets.cmake)
  if(NOT EXISTS
     "${install_prefix}/lib/cmake/Salts/${required_package_file}")
    message(FATAL_ERROR
            "Salts install is missing ${required_package_file}")
  endif()
endforeach()

file(GLOB installed_target_files
     "${install_prefix}/lib/cmake/Salts/SaltsTargets*.cmake")
foreach(installed_target_file IN LISTS installed_target_files)
  file(READ "${installed_target_file}" installed_target_contents)
  if(installed_target_contents MATCHES
     "(c-ares::|llhttp::|OpenSSL::|BoringSSL|[/\\\\]cares\\.(lib|a|so)|[/\\\\]llhttp[^/\\\\]*\\.(lib|a|so)|[/\\\\](ssl|crypto)[^/\\\\]*\\.(lib|a|so))")
    message(
      FATAL_ERROR
        "Salts installed target metadata exposes a private network dependency: ${installed_target_file}"
    )
  endif()
endforeach()

foreach(required_cstl_header IN ITEMS cstl.h cstl/typed.h cstl/stream.h)
  if(NOT EXISTS "${install_prefix}/include/${required_cstl_header}")
    message(FATAL_ERROR "Salts install is missing ${required_cstl_header}")
  endif()
endforeach()
foreach(required_core_header IN ITEMS
    clock.h
    thread_pool.h
    coroutine_module.h
    native_io.h)
  if(NOT EXISTS "${install_prefix}/include/salts/${required_core_header}")
    message(FATAL_ERROR "Salts install is missing salts/${required_core_header}")
  endif()
endforeach()
execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}"
          -S "${SOURCE_DIR}/tests/install_consumer"
          -B "${consumer_build}"
          -G "${BUILD_GENERATOR}"
          "-DCMAKE_BUILD_TYPE=${BUILD_CONFIG}"
          "-DCMAKE_PREFIX_PATH=${consumer_prefix_path}"
          "-DSalts_DIR=${install_prefix}/lib/cmake/Salts"
  RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR
          "Salts installed consumer configure failed: ${configure_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}"
          -S "${SOURCE_DIR}/tests/install_consumer"
          -B "${hidden_dependency_consumer_build}"
          -G "${BUILD_GENERATOR}"
          "-DCMAKE_BUILD_TYPE=${BUILD_CONFIG}"
          "-DCMAKE_PREFIX_PATH=${consumer_prefix_path}"
          "-DSalts_DIR=${install_prefix}/lib/cmake/Salts"
          "-DCMAKE_DISABLE_FIND_PACKAGE_c-ares=TRUE"
          "-DCMAKE_DISABLE_FIND_PACKAGE_llhttp=TRUE"
          "-DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=TRUE"
  RESULT_VARIABLE hidden_dependency_configure_result)
if(NOT hidden_dependency_configure_result EQUAL 0)
  message(FATAL_ERROR
          "Salts network targets leaked c-ares, llhttp, or BoringSSL to the installed consumer: ${hidden_dependency_configure_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}" --build "${consumer_build}"
          --config "${BUILD_CONFIG}"
  RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR
          "Salts installed consumer build failed: ${build_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND_PATH}" --build
          "${hidden_dependency_consumer_build}"
          --config "${BUILD_CONFIG}"
  RESULT_VARIABLE hidden_dependency_build_result)
if(NOT hidden_dependency_build_result EQUAL 0)
  message(FATAL_ERROR
          "Salts installed consumer without c-ares/llhttp/BoringSSL failed to build: ${hidden_dependency_build_result}")
endif()
