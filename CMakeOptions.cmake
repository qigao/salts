include(CMakeDependentOption)

set(CMAKE_COLOR_DIAGNOSTICS ON)

# building the tests
option(ENABLE_TESTS "Enable the tests" ON)

# Address Sanitizer - only enabled for Debug builds
cmake_dependent_option(ENABLE_ASAN "Enable Address Sanitizer" ON
                       "CMAKE_BUILD_TYPE STREQUAL Debug" OFF)

option(BUILD_EXAMPLES "Build example programs" ON)
option(BUILD_TESTS "Build test suite" ON)
cmake_dependent_option(BUILD_BENCHMARKS "Build benchmark executables" ON
                       "BUILD_TESTS" OFF)
option(CFLOW_ENABLE_MINICORO
       "Build the optional minicoro-backed CFlow Resumable adapter" OFF)

option(SALTS_ENABLE_EPOLL_READINESS
       "Enable the Linux epoll readiness backend" OFF)
if(SALTS_ENABLE_EPOLL_READINESS AND
   NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR
          "SALTS_ENABLE_EPOLL_READINESS is supported only on Linux")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
