cmake_minimum_required(VERSION 3.20)
project(CMetaSharedOwner VERSION 1.1.0 LANGUAGES C CXX)
include(CTest)
include(GNUInstallDirs)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

option(CMETA_OWNER_SANITIZERS "Check shared ownership with ASan and UBSan" OFF)
if(CMETA_OWNER_SANITIZERS)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR WIN32)
    message(FATAL_ERROR "CMETA_OWNER_SANITIZERS requires GCC/Clang on a Unix host")
  endif()
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
if(MSVC)
  add_compile_options(/experimental:c11atomics /Zc:preprocessor)
endif()

get_filename_component(SALTS_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include("${SALTS_SOURCE_ROOT}/cmake/CmakeUtils.cmake")
if(CMETA_PACKAGE_DIR)
  find_package(Salts CONFIG REQUIRED PATHS "${CMETA_PACKAGE_DIR}" NO_DEFAULT_PATH)
else()
  # Use the production target, not a test-owned copy of its source list.
  add_library(tinytest STATIC "${SALTS_SOURCE_ROOT}/tinytest/src/tinytest.c"
                             "${SALTS_SOURCE_ROOT}/tinytest/src/tinymock.c")
  add_library(Salts::TinyTest ALIAS tinytest)
  target_include_directories(tinytest PUBLIC "${SALTS_SOURCE_ROOT}/tinytest/include")
  set(BUILD_TESTS ON)
  add_subdirectory("${SALTS_SOURCE_ROOT}/cmeta" cmeta)
  install(EXPORT SaltsTargets FILE SaltsTargets.cmake NAMESPACE Salts::
          DESTINATION lib/cmake/Salts)
  file(WRITE "${CMAKE_BINARY_DIR}/SaltsConfig.cmake"
       "include(\"\${CMAKE_CURRENT_LIST_DIR}/SaltsTargets.cmake\")\n")
  install(FILES "${CMAKE_BINARY_DIR}/SaltsConfig.cmake" DESTINATION lib/cmake/Salts)
endif()

foreach(peer IN ITEMS a b)
  add_library(cmeta_owner_${peer} SHARED owner_peer.c)
  target_compile_definitions(cmeta_owner_${peer} PRIVATE
    CMETA_OWNER_DIRECT=cmeta_owner_${peer}_direct
    CMETA_OWNER_FIND=cmeta_owner_${peer}_find)
  target_link_libraries(cmeta_owner_${peer} PUBLIC Salts::CMeta)
endforeach()
add_executable(cmeta_shared_owner_test owner_test.c)
target_link_libraries(cmeta_shared_owner_test PRIVATE
  cmeta_owner_a cmeta_owner_b Salts::CMeta)
add_test(NAME cmeta_shared_owner_test COMMAND cmeta_shared_owner_test)

get_target_property(cmeta_owner_type Salts::CMeta TYPE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND cmeta_owner_type STREQUAL "STATIC_LIBRARY")
  add_test(NAME cmeta_static_exports_test
    COMMAND "${CMAKE_COMMAND}" "-DNM=${CMAKE_NM}"
      "-DPEER_A=$<TARGET_FILE:cmeta_owner_a>"
      "-DPEER_B=$<TARGET_FILE:cmeta_owner_b>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/check_exports.cmake")
endif()
