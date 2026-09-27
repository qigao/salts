add_library(salts_plugin_interface_representation_a SHARED
  interface_representation_a.c)
add_library(salts_plugin_interface_representation_b SHARED
  interface_representation_b.c)

foreach(rep_target IN ITEMS
    salts_plugin_interface_representation_a
    salts_plugin_interface_representation_b)
  target_compile_features(${rep_target} PRIVATE c_std_11)
  target_include_directories(${rep_target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${rep_target} PRIVATE Salts::CMeta)
  set_target_properties(${rep_target} PROPERTIES
    FOLDER "plugin/tests"
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
  if(WIN32)
    set_target_properties(${rep_target} PROPERTIES
      WINDOWS_EXPORT_ALL_SYMBOLS ON)
  endif()
endforeach()

cmake_add_test(salts_plugin_contract_test
  SOURCES plugin_contract_test.c
  LIBS
    Salts::Plugin
    Salts::TinyTest
    salts_plugin_interface_representation_a
    salts_plugin_interface_representation_b
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
  FOLDER "plugin/tests")

set_target_properties(salts_plugin_contract_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(salts_plugin_header_cpp_test
  SOURCES plugin_header_cpp_test.cpp
  LIBS Salts::Plugin
  FOLDER "plugin/tests")

set_target_properties(salts_plugin_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)


set(plugin_fixture_targets
  salts_plugin_fixture_c
  salts_plugin_fixture_cpp
  salts_plugin_fixture_missing_query
  salts_plugin_fixture_rejected
  salts_plugin_fixture_obsolete
  salts_plugin_fixture_invalid
  salts_plugin_fixture_lifecycle
  salts_plugin_fixture_partial_lifecycle
  salts_plugin_fixture_start_fail
  salts_plugin_fixture_stop_fail
  salts_plugin_fixture_slow_stop
  salts_plugin_fixture_slow_query_a
  salts_plugin_fixture_slow_query_b)

add_library(salts_plugin_fixture_c SHARED fixture_plugin_c.c)
add_library(salts_plugin_fixture_cpp SHARED fixture_plugin_cpp.cpp)
add_library(salts_plugin_fixture_missing_query SHARED
  fixture_plugin_missing_query.c)
add_library(salts_plugin_fixture_rejected SHARED
  fixture_plugin_rejected.c)
add_library(salts_plugin_fixture_obsolete SHARED
  fixture_plugin_obsolete.c)
add_library(salts_plugin_fixture_invalid SHARED
  fixture_plugin_invalid.c)
add_library(salts_plugin_fixture_lifecycle SHARED
  fixture_plugin_lifecycle.c)
add_library(salts_plugin_fixture_partial_lifecycle SHARED
  fixture_plugin_partial_lifecycle.c)
add_library(salts_plugin_fixture_start_fail SHARED
  fixture_plugin_start_fail.c)
add_library(salts_plugin_fixture_stop_fail SHARED
  fixture_plugin_stop_fail.c)
add_library(salts_plugin_fixture_slow_stop SHARED
  fixture_plugin_slow_stop.c)
add_library(salts_plugin_fixture_slow_query_a SHARED
  fixture_plugin_slow_query.c)
add_library(salts_plugin_fixture_slow_query_b SHARED
  fixture_plugin_slow_query.c)

target_compile_definitions(salts_plugin_fixture_slow_query_a PRIVATE
  PLUGIN_SLOW_QUERY_ENTERED_MARKER=PLUGIN_SLOW_QUERY_ENTERED_MARKER_A)
target_compile_definitions(salts_plugin_fixture_slow_query_b PRIVATE
  PLUGIN_SLOW_QUERY_ENTERED_MARKER=PLUGIN_SLOW_QUERY_ENTERED_MARKER_B)

foreach(fixture_target IN LISTS plugin_fixture_targets)
  target_include_directories(${fixture_target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
  target_link_libraries(${fixture_target} PRIVATE Salts::CMeta)
  set_target_properties(${fixture_target} PROPERTIES
    FOLDER "plugin/tests/fixtures")
endforeach()

set_target_properties(
  salts_plugin_fixture_c
  salts_plugin_fixture_missing_query
  salts_plugin_fixture_rejected
  salts_plugin_fixture_obsolete
  salts_plugin_fixture_invalid
  salts_plugin_fixture_lifecycle
  salts_plugin_fixture_partial_lifecycle
  salts_plugin_fixture_start_fail
  salts_plugin_fixture_stop_fail
  salts_plugin_fixture_slow_stop
  salts_plugin_fixture_slow_query_a
  salts_plugin_fixture_slow_query_b
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

set_target_properties(salts_plugin_fixture_cpp PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)

target_link_libraries(salts_plugin_fixture_c PRIVATE Salts::Platform)
target_link_libraries(salts_plugin_fixture_slow_stop PRIVATE Salts::Platform)
target_link_libraries(salts_plugin_fixture_slow_query_a PRIVATE Salts::Platform)
target_link_libraries(salts_plugin_fixture_slow_query_b PRIVATE Salts::Platform)

cmake_add_test(salts_plugin_loader_test
  SOURCES plugin_loader_test.c
  LIBS Salts::Plugin Salts::Platform Salts::TinyTest
  DEFS
    "PLUGIN_VALID_C_PATH=\"$<TARGET_FILE:salts_plugin_fixture_c>\""
    "PLUGIN_VALID_CPP_PATH=\"$<TARGET_FILE:salts_plugin_fixture_cpp>\""
    "PLUGIN_MISSING_QUERY_PATH=\"$<TARGET_FILE:salts_plugin_fixture_missing_query>\""
    "PLUGIN_REJECTED_PATH=\"$<TARGET_FILE:salts_plugin_fixture_rejected>\""
    "PLUGIN_OBSOLETE_PATH=\"$<TARGET_FILE:salts_plugin_fixture_obsolete>\""
    "PLUGIN_INVALID_PATH=\"$<TARGET_FILE:salts_plugin_fixture_invalid>\""
    "PLUGIN_LIFECYCLE_PATH=\"$<TARGET_FILE:salts_plugin_fixture_lifecycle>\""
    "PLUGIN_SLOW_QUERY_A_PATH=\"$<TARGET_FILE:salts_plugin_fixture_slow_query_a>\""
    "PLUGIN_SLOW_QUERY_B_PATH=\"$<TARGET_FILE:salts_plugin_fixture_slow_query_b>\""
  RUNTIME_TARGETS ${plugin_fixture_targets}
  FOLDER "plugin/tests")

add_dependencies(salts_plugin_loader_test ${plugin_fixture_targets})
set_target_properties(salts_plugin_loader_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)


cmake_add_test(salts_plugin_lifecycle_test
  SOURCES plugin_lifecycle_test.c
  LIBS Salts::Plugin Salts::Platform Salts::TinyTest
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
  DEFS
    "PLUGIN_LIFECYCLE_PATH=\"$<TARGET_FILE:salts_plugin_fixture_lifecycle>\""
    "PLUGIN_PARTIAL_LIFECYCLE_PATH=\"$<TARGET_FILE:salts_plugin_fixture_partial_lifecycle>\""
    "PLUGIN_START_FAIL_PATH=\"$<TARGET_FILE:salts_plugin_fixture_start_fail>\""
    "PLUGIN_STOP_FAIL_PATH=\"$<TARGET_FILE:salts_plugin_fixture_stop_fail>\""
    "PLUGIN_SLOW_STOP_PATH=\"$<TARGET_FILE:salts_plugin_fixture_slow_stop>\""
  RUNTIME_TARGETS ${plugin_fixture_targets}
  FOLDER "plugin/tests")

add_dependencies(salts_plugin_lifecycle_test ${plugin_fixture_targets})
set_target_properties(salts_plugin_lifecycle_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
