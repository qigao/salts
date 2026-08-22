# Common module tests

# Collect test files
set(TLOG_HEADER_COLLISION_C
    ${CMAKE_CURRENT_SOURCE_DIR}/test_tlog_header_collision.c)
set(TLOG_HEADER_COLLISION_CPP
    ${CMAKE_CURRENT_SOURCE_DIR}/test_tlog_header_collision.cpp)
set(FMT_SURFACE_CONTRACT_C
    ${CMAKE_CURRENT_SOURCE_DIR}/test_fmt_surface_contract.c)
set(FMT_SURFACE_CONTRACT_CPP
    ${CMAKE_CURRENT_SOURCE_DIR}/test_fmt_surface_contract.cpp)
set(EXECUTION_COMPAT_C
    ${CMAKE_CURRENT_SOURCE_DIR}/test_execution_compat.c)
cmake_add_source(TEST_SOURCES DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS
                 "test_*.c" EXCLUDES ${TLOG_HEADER_COLLISION_C} ${FMT_SURFACE_CONTRACT_C}
                 ${EXECUTION_COMPAT_C})
cmake_add_source(CXX_TEST_SOURCES DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS
                 "test_*.cpp" EXCLUDES ${TLOG_HEADER_COLLISION_CPP} ${FMT_SURFACE_CONTRACT_CPP})

# Common libraries for all tests
set(COMMON_LIBS TurboUtils::TinyTest TurboUtils::Core)
if(WIN32)
  list(APPEND COMMON_LIBS psapi iphlpapi userenv ws2_32)
endif()

# Create test executables
foreach(test_source ${TEST_SOURCES} ${CXX_TEST_SOURCES})
  get_filename_component(test_name ${test_source} NAME_WE)
  cmake_add_test(${test_name}
    SOURCES ${test_source}
    FOLDER "utils/tests"
    LIBS ${COMMON_LIBS})
endforeach()

# This link contract intentionally depends only on Core. Its legacy headers
# must pull Platform/Concurrency transitively through the exported target graph.
cmake_add_test(test_execution_compat
  SOURCES ${EXECUTION_COMPAT_C}
  FOLDER "utils/tests"
  LIBS TurboUtils::Core)
set_target_properties(test_execution_compat PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND TARGET test_tlog)
  target_compile_options(test_tlog PRIVATE -Werror=null-dereference)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  foreach(tgt test_tlog_cpp test_turbo_str_meta_cpp)
    if(TARGET ${tgt})
      target_compile_options(${tgt} PRIVATE -Werror=old-style-cast)
    endif()
  endforeach()
endif()

# fmt surface contracts deliberately do not link TurboUtils::Core. They stub
# fmt_print and validate the public macro expansion contract. Because fmt.h
# reaches the Core compatibility headers directly, these standalone targets
# explicitly own both canonical execution-substrate include contracts.
cmake_add_test(test_fmt_surface_contract_c
  SOURCES ${FMT_SURFACE_CONTRACT_C}
  FOLDER "utils/tests"
  LIBS TurboUtils::CMeta TurboUtils::Platform TurboUtils::Concurrency)
target_include_directories(test_fmt_surface_contract_c PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../parser
  ${CMAKE_SOURCE_DIR}/vendor/sds)
set_target_properties(test_fmt_surface_contract_c PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(test_fmt_surface_contract_c PRIVATE
    -Werror=unused-parameter
    -Werror=unused-variable)
endif()

cmake_add_test(test_fmt_surface_contract_cpp
  SOURCES ${FMT_SURFACE_CONTRACT_CPP}
  FOLDER "utils/tests"
  LIBS TurboUtils::CMeta TurboUtils::Platform TurboUtils::Concurrency)
target_include_directories(test_fmt_surface_contract_cpp PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../parser
  ${CMAKE_SOURCE_DIR}/vendor/sds)
set_target_properties(test_fmt_surface_contract_cpp PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(test_fmt_surface_contract_cpp PRIVATE
    -Werror=sign-conversion
    -Werror=old-style-cast
    -Werror=missing-field-initializers
    -Werror=unused-parameter
    -Werror=unused-variable)
endif()

if(TARGET test_turbo_str_meta)
  target_sources(test_turbo_str_meta PRIVATE turbo_str_meta_peer.c)
endif()

cmake_add_test(test_tlog_header_collision_c
  SOURCES ${TLOG_HEADER_COLLISION_C}
  FOLDER "utils/tests"
  LIBS ${COMMON_LIBS})
set_target_properties(test_tlog_header_collision_c PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(test_tlog_header_collision_cpp
  SOURCES ${TLOG_HEADER_COLLISION_CPP}
  FOLDER "utils/tests"
  LIBS ${COMMON_LIBS})
set_target_properties(test_tlog_header_collision_cpp PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)

# Link specific libraries for certain tests
foreach(tgt test_fmt_re2c test_fmt)
  if(TARGET ${tgt})
    target_link_libraries(${tgt} PRIVATE fmt_lexer)
  endif()
endforeach()

foreach(tgt test_turbo_str)
  if(TARGET ${tgt})
    target_link_libraries(${tgt} PRIVATE TurboUtils::SDS)
  endif()
endforeach()

# test_re exercises the internal SIMDe first-byte scanner (utils/src/re_scan.h)
if(TARGET test_re)
  target_include_directories(test_re PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${SIMDE_INCLUDE_DIRS})
endif()
