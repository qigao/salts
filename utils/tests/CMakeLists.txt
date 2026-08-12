# Common module tests

# Collect test files
cmake_add_source(TEST_SOURCES DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS
                 "test_*.c")
cmake_add_source(CXX_TEST_SOURCES DIRS ${CMAKE_CURRENT_SOURCE_DIR} PATTERNS
                 "test_*.cpp")

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
