cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 OLD)
endif()
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

project(
  Salts
  VERSION 1.1.0
  LANGUAGES C CXX)

# Project configuration - output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Normalize install prefix (remove trailing slashes)
get_filename_component(CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}" ABSOLUTE)

# Project description
set(PROJECT_DESCRIPTION "common library for C/C++")
set(PROJECT_HOMEPAGE_URL "https://github.com/qigao/salts")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
# Language standards - unified approach
set(CMAKE_CXX_STANDARD 17) # C++17 for compatibility
set(CMAKE_C_STANDARD 11) # C17 to ensure C11 atomic support
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  add_compile_options(/experimental:c11atomics)
endif()
# Enable folder organization in IDEs
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "cmake")

# Enable testing
include(CTest)
enable_testing()

include(CMakeOptions.cmake)
include(Sanitizers)
include(CmakeUtils)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Additional components
add_subdirectory(tools)
include(FindTools)
add_subdirectory(vendor)
add_subdirectory(tinytest)
add_subdirectory(platform)
add_subdirectory(concurrency)
add_subdirectory(coroutine)
add_subdirectory(native-io)
add_subdirectory(parser/uri_parser)
add_subdirectory(cmeta)
add_subdirectory(cflow)
add_subdirectory(cnet)
add_subdirectory(chttp)
add_subdirectory(cserde)
add_subdirectory(cbind)
add_subdirectory(cstl)
add_subdirectory(utils)
add_subdirectory(cflow-process)
add_subdirectory(parser)
add_subdirectory(s3)
add_subdirectory(crpc)
add_subdirectory(cflow-fs)

set(verify_installed_dependencies
    salts_platform
    salts_concurrency
    salts_coroutine
    salts_native_io
    salts_cnet
    salts_chttp
    salts_cmeta
    salts_cserde
    salts_cbind
    salts_cflow
    salts_cstl
    salts_cstl_stream
    salts
    salts_cflow_process
    salts_cflow_fs
    salts_crpc
    ${SALTS_PARSER_TARGETS}
    salts_selector)
add_custom_target(
  verify_installed_package
  COMMAND
    ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DBUILD_DIR=${CMAKE_BINARY_DIR}
    -DCMAKE_COMMAND_PATH=${CMAKE_COMMAND}
    -DBUILD_CONFIG=$<CONFIG>
    -DBUILD_GENERATOR=${CMAKE_GENERATOR}
    "-DDEPENDENCY_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
    -P
    ${CMAKE_SOURCE_DIR}/cmake/VerifyInstalledPackage.cmake
  DEPENDS ${verify_installed_dependencies}
  VERBATIM)
set_target_properties(verify_installed_package PROPERTIES FOLDER "cmake")

# Package Configuration
include(CMakePackageConfigHelpers)

# Generate the config file
configure_package_config_file(
  "${CMAKE_SOURCE_DIR}/cmake/SaltsConfig.cmake.in"
  "${CMAKE_BINARY_DIR}/SaltsConfig.cmake"
  INSTALL_DESTINATION "lib/cmake/Salts")

# Generate the version file
write_basic_package_version_file(
  "${CMAKE_BINARY_DIR}/SaltsConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion)

# Install the config files
install(FILES "${CMAKE_BINARY_DIR}/SaltsConfig.cmake"
              "${CMAKE_BINARY_DIR}/SaltsConfigVersion.cmake"
        DESTINATION "lib/cmake/Salts")

# Install the export set
export(
  EXPORT SaltsTargets
  FILE "${CMAKE_BINARY_DIR}/SaltsTargets.cmake"
  NAMESPACE Salts::)

install(
  EXPORT SaltsTargets
  FILE SaltsTargets.cmake
  NAMESPACE Salts::
  DESTINATION "lib/cmake/Salts")
