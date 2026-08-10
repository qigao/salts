cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 OLD)
endif()
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

project(
  TurboUtils
  VERSION 1.0.0
  LANGUAGES C CXX)

# Project configuration - output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Normalize install prefix (remove trailing slashes)
get_filename_component(CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}" ABSOLUTE)

# Project description
set(PROJECT_DESCRIPTION "common library for C/C++")
set(PROJECT_HOMEPAGE_URL "https://github.com/qigao/turboutils")

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
add_subdirectory(parser/query_vm)
add_subdirectory(vendor)
add_subdirectory(tinytest)
add_subdirectory(parser)
add_subdirectory(turbo_parser)
add_subdirectory(mustache)
add_subdirectory(utils)
add_subdirectory(cron)
add_subdirectory(tbe)

# Package Configuration
include(CMakePackageConfigHelpers)

# Generate the config file
configure_package_config_file(
  "${CMAKE_SOURCE_DIR}/cmake/TurboUtilsConfig.cmake.in"
  "${CMAKE_BINARY_DIR}/TurboUtilsConfig.cmake"
  INSTALL_DESTINATION "lib/cmake/TurboUtils")

# Generate the version file
write_basic_package_version_file(
  "${CMAKE_BINARY_DIR}/TurboUtilsConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion)

# Install the config files
install(FILES "${CMAKE_BINARY_DIR}/TurboUtilsConfig.cmake"
              "${CMAKE_BINARY_DIR}/TurboUtilsConfigVersion.cmake"
        DESTINATION "lib/cmake/TurboUtils")

# Install the export set
export(
  EXPORT TurboUtilsTargets
  FILE "${CMAKE_BINARY_DIR}/TurboUtilsTargets.cmake"
  NAMESPACE TurboUtils::)

install(
  EXPORT TurboUtilsTargets
  FILE TurboUtilsTargets.cmake
  NAMESPACE TurboUtils::
  DESTINATION "lib/cmake/TurboUtils")
