include("${CMAKE_CURRENT_LIST_DIR}/linker_tests.cmake")

add_library(salts_plugin_manifest_fixture_c SHARED
  fixture_plugin_manifest_bridge.c fixture_plugin_manifest_c.c)
add_library(salts_plugin_manifest_fixture_cpp SHARED
  fixture_plugin_manifest_bridge.c fixture_plugin_manifest_cpp.cpp)
foreach(manifest_target IN ITEMS salts_plugin_manifest_fixture_c salts_plugin_manifest_fixture_cpp)
  target_include_directories(${manifest_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../include")
  target_link_libraries(${manifest_target} PRIVATE Salts::CMeta)
  set_target_properties(${manifest_target} PROPERTIES FOLDER "plugin/tests/fixtures"
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
endforeach()
target_compile_definitions(salts_plugin_manifest_fixture_c PRIVATE
  "PLUGIN_DECLARATION_ID=\"test.plugin.manifest.c\"")
target_compile_definitions(salts_plugin_manifest_fixture_cpp PRIVATE
  "PLUGIN_DECLARATION_ID=\"test.plugin.manifest.cpp\"")
cmake_add_test(salts_plugin_manifest_test SOURCES plugin_manifest_test.c
  LIBS Salts::Plugin Salts::TinyTest
  DEFS "PLUGIN_MANIFEST_C_PATH=\"$<TARGET_FILE:salts_plugin_manifest_fixture_c>\""
       "PLUGIN_MANIFEST_CPP_PATH=\"$<TARGET_FILE:salts_plugin_manifest_fixture_cpp>\""
  FOLDER "plugin/tests")
add_dependencies(salts_plugin_manifest_test salts_plugin_manifest_fixture_c salts_plugin_manifest_fixture_cpp)
set_target_properties(salts_plugin_manifest_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
set_tests_properties(salts_plugin_manifest_test PROPERTIES TIMEOUT 60)

cmake_add_test(salts_plugin_declaration_test
  SOURCES plugin_declaration_test.c
  LIBS Salts::Plugin Salts::TinyTest FOLDER "plugin/tests")
cmake_add_test(salts_plugin_declaration_cpp_test
  SOURCES plugin_declaration_cpp_test.cpp
  LIBS Salts::Plugin Salts::TinyTest FOLDER "plugin/tests")
set_target_properties(salts_plugin_declaration_test PROPERTIES C_EXTENSIONS OFF)
set_target_properties(salts_plugin_declaration_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

foreach(empty_mode IN ITEMS passive managed)
  foreach(empty_language IN ITEMS c cpp)
    set(empty_target "salts_plugin_empty_${empty_mode}_${empty_language}_test")
    if(empty_language STREQUAL "c")
      set(empty_source plugin_empty_declaration_test.c)
    else()
      set(empty_source plugin_empty_declaration_cpp_test.cpp)
    endif()
    cmake_add_test(${empty_target} SOURCES ${empty_source}
      LIBS Salts::Plugin Salts::TinyTest FOLDER "plugin/tests")
    set_target_properties(${empty_target} PROPERTIES
      C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
      CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    if(empty_mode STREQUAL "managed")
      target_compile_definitions(${empty_target} PRIVATE PLUGIN_EMPTY_MANAGED=1)
    endif()
  endforeach()
endforeach()

set(plugin_declaration_version_diagnostic "Plugin contract version must fit")
set(plugin_declaration_carrier_diagnostic "Plugin interface carrier type mismatch")
set(plugin_declaration_callback_diagnostic "Plugin start callback type mismatch")
set(plugin_declaration_parameter_diagnostic "CMeta exact invoke requires explicit parameter ABI carriers")
set(plugin_declaration_result_diagnostic "CMeta exact invoke result carrier mismatch")
set(plugin_declaration_array_diagnostic "CMeta exact invoke requires an unqualified value parameter carrier")
set(plugin_declaration_semver_major_diagnostic "Plugin semantic version component must fit")
set(plugin_declaration_semver_minor_diagnostic "${plugin_declaration_semver_major_diagnostic}")
set(plugin_declaration_semver_patch_diagnostic "${plugin_declaration_semver_major_diagnostic}")
set(plugin_declaration_capabilities_diagnostic "Plugin capabilities must fit")
foreach(declaration_case IN ITEMS version carrier callback parameter result array
    semver_major semver_minor semver_patch capabilities)
  foreach(declaration_language IN ITEMS c cpp)
    set(declaration_target "salts_plugin_declaration_${declaration_case}")
    if(declaration_language STREQUAL "cpp")
      string(APPEND declaration_target "_cpp")
      set(declaration_source compile_fail/plugin_declaration_cpp.cpp)
    else()
      set(declaration_source "compile_fail/plugin_declaration_${declaration_case}.c")
    endif()
    string(APPEND declaration_target "_compile_fail")
    add_executable(${declaration_target} EXCLUDE_FROM_ALL ${declaration_source})
    target_link_libraries(${declaration_target} PRIVATE Salts::PluginABI)
    if(declaration_language STREQUAL "cpp")
      target_compile_definitions(${declaration_target} PRIVATE
        "PLUGIN_DECLARATION_SOURCE=\"plugin_declaration_${declaration_case}.c\"")
    endif()
    set_target_properties(${declaration_target} PROPERTIES
      C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
      CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
      FOLDER "plugin/tests/compile_fail")
    add_test(NAME ${declaration_target}
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
        --config $<CONFIG> --target ${declaration_target})
    set_tests_properties(${declaration_target} PROPERTIES RUN_SERIAL TRUE
      PASS_REGULAR_EXPRESSION "${plugin_declaration_${declaration_case}_diagnostic}")
  endforeach()
endforeach()

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
  salts_plugin_fixture_object_interface
  salts_plugin_fixture_generic_graph
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
add_library(salts_plugin_fixture_object_interface SHARED
  fixture_plugin_object_interface.c)
add_library(salts_plugin_fixture_generic_graph SHARED
  fixture_plugin_generic_graph.c)
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
  salts_plugin_fixture_object_interface
  salts_plugin_fixture_generic_graph
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
    "PLUGIN_OBJECT_INTERFACE_PATH=\"$<TARGET_FILE:salts_plugin_fixture_object_interface>\""
    "PLUGIN_GENERIC_GRAPH_PATH=\"$<TARGET_FILE:salts_plugin_fixture_generic_graph>\""
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


if(WIN32)
  set(SALTS_PLUGIN_PRIVATE_DEPENDENCY_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/private_dependency")

  add_library(salts_plugin_fixture_private_dependency_helper SHARED
    fixture_plugin_private_dependency_helper.c)
  set_target_properties(salts_plugin_fixture_private_dependency_helper PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${SALTS_PLUGIN_PRIVATE_DEPENDENCY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/private_dependency_import"
    FOLDER "plugin/tests/fixtures")

  add_library(salts_plugin_fixture_private_dependency SHARED
    fixture_plugin_private_dependency.c)
  target_include_directories(salts_plugin_fixture_private_dependency PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
  target_link_libraries(salts_plugin_fixture_private_dependency PRIVATE
    Salts::CMeta
    salts_plugin_fixture_private_dependency_helper)
  set_target_properties(salts_plugin_fixture_private_dependency PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${SALTS_PLUGIN_PRIVATE_DEPENDENCY_DIR}"
    FOLDER "plugin/tests/fixtures")

  cmake_add_test(salts_plugin_windows_dependency_test
    SOURCES plugin_windows_dependency_test.c
    LIBS Salts::Plugin Salts::TinyTest
    DEFS
      "PLUGIN_PRIVATE_DEPENDENCY_PATH=\"$<TARGET_FILE:salts_plugin_fixture_private_dependency>\""
    FOLDER "plugin/tests")

  add_dependencies(salts_plugin_windows_dependency_test
    salts_plugin_fixture_private_dependency
    salts_plugin_fixture_private_dependency_helper)
  set_target_properties(salts_plugin_windows_dependency_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
endif()
