if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.20)
  project(SaltsCMetaLowerHost LANGUAGES C)
endif()

if(CMAKE_CROSSCOMPILING)
  include(ExternalProject)

  set(cmeta_lower_host_binary_dir "${CMAKE_CURRENT_BINARY_DIR}/host-build")
  if(CMAKE_HOST_WIN32)
    foreach(required_environment MSVC_ROOT WINDOWS_SDK_ROOT WINDOWS_SDK_VERSION INCLUDE LIB)
      if("$ENV{${required_environment}}" STREQUAL "")
        message(FATAL_ERROR
                "The cmeta-lower host build requires ${required_environment} in the configure environment")
      endif()
    endforeach()
    set(cmeta_lower_host_executable "${cmeta_lower_host_binary_dir}/cmeta-lower.exe")
    set(cmeta_lower_host_toolchain_args
        "-DCMAKE_C_COMPILER:FILEPATH=$ENV{MSVC_ROOT}/bin/Hostx64/x64/cl.exe"
        "-DCMAKE_LINKER:FILEPATH=$ENV{MSVC_ROOT}/bin/Hostx64/x64/link.exe"
        "-DCMAKE_AR:FILEPATH=$ENV{MSVC_ROOT}/bin/Hostx64/x64/lib.exe"
        "-DCMAKE_RC_COMPILER:FILEPATH=$ENV{WINDOWS_SDK_ROOT}/bin/$ENV{WINDOWS_SDK_VERSION}/x64/rc.exe"
        "-DCMAKE_MT:FILEPATH=$ENV{WINDOWS_SDK_ROOT}/bin/$ENV{WINDOWS_SDK_VERSION}/x64/mt.exe")
  else()
    find_program(cmeta_lower_host_c_compiler NAMES cc gcc clang REQUIRED)
    set(cmeta_lower_host_executable "${cmeta_lower_host_binary_dir}/cmeta-lower")
    set(cmeta_lower_host_toolchain_args
        "-DCMAKE_C_COMPILER:FILEPATH=${cmeta_lower_host_c_compiler}")
  endif()

  ExternalProject_Add(
    salts_cmeta_lower_host
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    BINARY_DIR "${cmeta_lower_host_binary_dir}"
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    CMAKE_CACHE_ARGS
      "-DCMAKE_BUILD_TYPE:STRING=Release"
      "-DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}"
      ${cmeta_lower_host_toolchain_args}
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target cmeta_lower --config Release
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${cmeta_lower_host_executable}")

  add_executable(cmeta_lower IMPORTED GLOBAL)
  set_target_properties(cmeta_lower PROPERTIES
    IMPORTED_LOCATION "${cmeta_lower_host_executable}")
  add_dependencies(cmeta_lower salts_cmeta_lower_host)
else()
  add_executable(cmeta_lower cmeta_lower.c)
  target_compile_features(cmeta_lower PRIVATE c_std_11)
  set_target_properties(cmeta_lower PROPERTIES
    OUTPUT_NAME "cmeta-lower"
    FOLDER "tools/cmeta-lower")

  install(TARGETS cmeta_lower
    RUNTIME DESTINATION bin
    COMPONENT tools)
endif()

add_executable(Salts::CMetaLower ALIAS cmeta_lower)
