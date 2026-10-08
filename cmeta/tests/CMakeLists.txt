cmake_add_test(cmeta_data_reflect_test
  SOURCES cmeta_data_reflect_test.c cmeta_data_reflect_peer.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")
cmake_add_test(cmeta_data_reflect_cpp_test
  SOURCES cmeta_data_reflect_cpp_test.cpp cmeta_data_reflect_peer.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

cmake_add_test(cmeta_fastpath_pattern_test
  SOURCES cmeta_pattern_test.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")
cmake_add_test(cmeta_fastpath_pattern_cpp_test
  SOURCES cmeta_pattern_cpp_test.cpp
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")

# Post-freeze POSA2 CMeta composition: typed locking policy, synchronized
# public Interface and real Monitor Object with bounded wait/close semantics.
cmake_add_test(cmeta_ace_sync_test
  SOURCES cmeta_ace_sync_test.c
  LIBS Salts::CMeta Salts::Platform Salts::TinyTest
  FOLDER "cmeta/tests")
cmake_add_test(cmeta_ace_sync_cpp_test
  SOURCES cmeta_ace_sync_cpp_test.cpp
  LIBS Salts::CMeta Salts::Platform Salts::TinyTest
  FOLDER "cmeta/tests")
set_target_properties(cmeta_ace_sync_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_target_properties(cmeta_ace_sync_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
set_tests_properties(cmeta_ace_sync_test cmeta_ace_sync_cpp_test PROPERTIES TIMEOUT 60)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cmeta_ace_sync_cpp_test PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-Werror=old-style-cast>")
endif()

cmake_add_test(cmeta_fastpath_component_test
  SOURCES cmeta_component_test.c
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")
cmake_add_test(cmeta_fastpath_component_cpp_test
  SOURCES cmeta_component_cpp_test.cpp
  LIBS Salts::CMeta Salts::TinyTest
  FOLDER "cmeta/tests")
set_target_properties(cmeta_fastpath_pattern_test cmeta_fastpath_component_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_target_properties(cmeta_fastpath_pattern_cpp_test cmeta_fastpath_component_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cmeta_fastpath_pattern_cpp_test PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-Werror=old-style-cast>")
  target_compile_options(cmeta_fastpath_component_cpp_test PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-Werror=old-style-cast>")
endif()

# ACE feature-freeze compile admission: the canonical Interface method
# signature must compile under C11 and C++17; an incompatible native
# return-type assertion must fail. FunctionAbi metadata is covered by the
# existing C11/C++17 pattern runtime suites.
# This runs at configure time only on native hosts (mobile cross builds retain
# the ordinary public-header compile tests).
if(NOT CMAKE_CROSSCOMPILING)
  include(CheckCSourceCompiles)
  include(CheckCXXSourceCompiles)

  set(_salts_ace_previous_required_includes "${CMAKE_REQUIRED_INCLUDES}")
  set(_salts_ace_previous_try_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
  set(CMAKE_REQUIRED_INCLUDES "${PROJECT_SOURCE_DIR}/cmeta/include")
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

  # Native signature admission uses an existing, widely exercised R0
  # CMeta Interface declaration. Compare its canonical generated vtable member
  # to the exact declared type; the bad case must reject void return. Reflected
  # FunctionAbi result-carrier runtime facts are checked by cmeta_pattern_test.
  # No custom pattern facade or duplicate signature ABI is introduced.
  set(_salts_ace_good_interface_source [=[
#include <cmeta/function.h>
#include <cmeta/object.h>
#include <cmeta/object_interface.h>
#define ACE_SCORE_METHODS(X,I) X(I,R0,int,get,_)
CMETA_INTERFACE(ace_freeze_score, ACE_SCORE_METHODS);
CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES(
    CMETA_IFACE_MEMBER_ADDRESS_(ace_freeze_score,get),
    int (**)(void *)), "ACE exact CMeta Strategy method");
int main(void) { return 0; }
]=])
  set(_salts_ace_bad_interface_source [=[
#include <cmeta/function.h>
#include <cmeta/object.h>
#include <cmeta/object_interface.h>
#define ACE_SCORE_METHODS(X,I) X(I,R0,int,get,_)
CMETA_INTERFACE(ace_freeze_score, ACE_SCORE_METHODS);
CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES(
    CMETA_IFACE_MEMBER_ADDRESS_(ace_freeze_score,get),
    void (**)(void *)), "ACE must reject wrong native return signature");
int main(void) { return 0; }
]=])

  # Re-evaluate on configuration: a compiler/SDK change must not silently
  # inherit a stale cached negative result from an earlier branch revision.
  foreach(_ace_check IN ITEMS
      SALTS_ACE_GOOD_C11_COMPILES SALTS_ACE_BAD_C11_COMPILES
      SALTS_ACE_GOOD_CPP17_COMPILES SALTS_ACE_BAD_CPP17_COMPILES)
    unset("${_ace_check}" CACHE)
  endforeach()

  check_c_source_compiles("${_salts_ace_good_interface_source}"
    SALTS_ACE_GOOD_C11_COMPILES)
  check_c_source_compiles("${_salts_ace_bad_interface_source}"
    SALTS_ACE_BAD_C11_COMPILES)
  check_cxx_source_compiles("${_salts_ace_good_interface_source}"
    SALTS_ACE_GOOD_CPP17_COMPILES)
  check_cxx_source_compiles("${_salts_ace_bad_interface_source}"
    SALTS_ACE_BAD_CPP17_COMPILES)

  set(CMAKE_REQUIRED_INCLUDES "${_salts_ace_previous_required_includes}")
  if("${_salts_ace_previous_try_type}" STREQUAL "")
    unset(CMAKE_TRY_COMPILE_TARGET_TYPE)
  else()
    set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_salts_ace_previous_try_type}")
  endif()

  if(NOT SALTS_ACE_GOOD_C11_COMPILES OR SALTS_ACE_BAD_C11_COMPILES OR
     NOT SALTS_ACE_GOOD_CPP17_COMPILES OR SALTS_ACE_BAD_CPP17_COMPILES)
    # Make a false-negative positive sample actionable on all hosts.
    set(_ace_compiler_log "${CMAKE_BINARY_DIR}/CMakeFiles/CMakeConfigureLog.yaml")
    if(NOT EXISTS "${_ace_compiler_log}")
      set(_ace_compiler_log "${CMAKE_BINARY_DIR}/CMakeFiles/CMakeError.log")
    endif()
    if(EXISTS "${_ace_compiler_log}")
      file(READ "${_ace_compiler_log}" _ace_diagnostics)
      string(FIND "${_ace_diagnostics}" "SALTS_ACE_GOOD_C11_COMPILES" _ace_start)
      if(_ace_start GREATER_EQUAL 0)
        string(LENGTH "${_ace_diagnostics}" _ace_log_size)
        math(EXPR _ace_remain "${_ace_log_size} - ${_ace_start}")
        if(_ace_remain GREATER 5500)
          set(_ace_remain 5500)
        endif()
        string(SUBSTRING "${_ace_diagnostics}" ${_ace_start} ${_ace_remain} _ace_excerpt)
        message(WARNING "ACE positive compile probe diagnostic:\n${_ace_excerpt}")
      endif()
    endif()
    message(FATAL_ERROR
      "ACE CMeta Interface native signature compile admission failed: "
      "valid C11=${SALTS_ACE_GOOD_C11_COMPILES}, invalid C11=${SALTS_ACE_BAD_C11_COMPILES}, "
      "valid C++17=${SALTS_ACE_GOOD_CPP17_COMPILES}, invalid C++17=${SALTS_ACE_BAD_CPP17_COMPILES}")
  endif()
endif()

cmake_add_test(cmeta_trace_test
  SOURCES cmeta_trace_test.c
  LIBS Salts::CMeta Salts::TinyTest Salts::Platform
  FOLDER "cmeta/tests")
cmake_add_test(cmeta_trace_cpp_test
  SOURCES cmeta_trace_cpp_test.cpp cmeta_trace_peer.c
  LIBS Salts::CMeta Salts::TinyTest Salts::Platform
  FOLDER "cmeta/tests")
set_target_properties(cmeta_trace_test cmeta_trace_cpp_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_target_properties(cmeta_trace_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
set_tests_properties(cmeta_trace_test PROPERTIES TIMEOUT 60)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cmeta_trace_cpp_test PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-Werror=old-style-cast>")
endif()

cmake_add_test(cmeta_fastpath_test
  SOURCES cmeta_fastpath_test.c cmeta_fastpath_peer.c
  LIBS Salts::CMeta Salts::TinyTest Salts::Platform
  FOLDER "cmeta/tests")
cmake_add_test(cmeta_fastpath_cpp_test
  SOURCES cmeta_fastpath_cpp_test.cpp cmeta_fastpath_peer.c
  LIBS Salts::CMeta Salts::TinyTest Salts::Platform
  FOLDER "cmeta/tests")
cmake_add_test(cmeta_fastpath_coroutine_test
  SOURCES cmeta_fastpath_coroutine_test.c cmeta_fastpath_peer.c
  LIBS Salts::CMeta Salts::TinyTest Salts::Coroutine Salts::Platform
  FOLDER "cmeta/tests")
set_target_properties(cmeta_fastpath_test cmeta_fastpath_cpp_test cmeta_fastpath_coroutine_test
  PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_target_properties(cmeta_fastpath_cpp_test
  PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(cmeta_fastpath_cpp_test PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-Werror=old-style-cast>")
endif()