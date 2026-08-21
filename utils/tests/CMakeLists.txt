# Common module tests

# Collect test files
set(TLOG_HEADER_COLLISION_C
    ${CMAKE_CURRENT_SOURCE_DIR}/test_tlog_header_collision.c)
set(TLOG_HEADER_COLLISION_CPP
    ${CMAKE_CURRENT_SOURCE_DIR}/test_tlog_header_collision.cpp)
cmake_add_source(TEST_SOURCES DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS
                 "test_*.c" EXCLUDES ${TLOG_HEADER_COLLISION_C})
cmake_add_source(CXX_TEST_SOURCES DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS
                 "test_*.cpp" EXCLUDES ${TLOG_HEADER_COLLISION_CPP})

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
