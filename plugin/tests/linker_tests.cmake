# Shared by the native graph and the installed SDK consumer graph. All public
# headers come from the linked targets, including in the installed tests.
set(plugin_linker_test_dir "${CMAKE_CURRENT_LIST_DIR}")
foreach(linker_variant IN ITEMS c cpp missing count)
  set(linker_target "cmeta_plugin_linker_fixture_${linker_variant}")
  add_library(${linker_target} SHARED "${plugin_linker_test_dir}/fixture_plugin_linker_a.c")
  if(linker_variant STREQUAL "cpp")
    target_sources(${linker_target} PRIVATE "${plugin_linker_test_dir}/fixture_plugin_linker_b.cpp"
      "${plugin_linker_test_dir}/fixture_plugin_linker_root.cpp")
  else()
    target_sources(${linker_target} PRIVATE "${plugin_linker_test_dir}/fixture_plugin_linker_root.c")
    if(NOT linker_variant STREQUAL "missing")
      target_sources(${linker_target} PRIVATE "${plugin_linker_test_dir}/fixture_plugin_linker_b.c")
    endif()
  endif()
  set(linker_count PLUGIN_LINKER_EXPORT_COUNT)
  if(linker_variant STREQUAL "count")
    set(linker_count "(PLUGIN_LINKER_EXPORT_COUNT-1u)")
  endif()
  target_compile_definitions(${linker_target} PRIVATE
    "PLUGIN_LINKER_ID=\"test.plugin.linker.${linker_variant}\""
    "PLUGIN_LINKER_COUNT=${linker_count}")
  target_link_libraries(${linker_target} PRIVATE Salts::PluginABI)
  set_target_properties(${linker_target} PROPERTIES FOLDER "plugin/tests/fixtures"
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  if(WIN32)
    target_link_options(${linker_target} PRIVATE "LINKER:/OPT:REF" "LINKER:/INCREMENTAL:NO")
  elseif(APPLE)
    target_link_options(${linker_target} PRIVATE "LINKER:-dead_strip")
  else()
    target_compile_options(${linker_target} PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(${linker_target} PRIVATE "LINKER:--gc-sections")
  endif()
endforeach()

cmake_add_test(cmeta_plugin_linker_test
  SOURCES "${plugin_linker_test_dir}/plugin_linker_test.c"
    "${plugin_linker_test_dir}/fixture_plugin_linker_root.c"
    "${plugin_linker_test_dir}/fixture_plugin_linker_a.c"
    "${plugin_linker_test_dir}/fixture_plugin_linker_b.c"
  LIBS Salts::Plugin Salts::TinyTest
  DEFS
    "PLUGIN_LINKER_ID=\"test.plugin.linker.executable\"" "PLUGIN_LINKER_COUNT=PLUGIN_LINKER_EXPORT_COUNT"
    "PLUGIN_LINKER_C_PATH=\"$<TARGET_FILE:cmeta_plugin_linker_fixture_c>\""
    "PLUGIN_LINKER_CPP_PATH=\"$<TARGET_FILE:cmeta_plugin_linker_fixture_cpp>\""
    "PLUGIN_LINKER_MISSING_PATH=\"$<TARGET_FILE:cmeta_plugin_linker_fixture_missing>\""
    "PLUGIN_LINKER_COUNT_PATH=\"$<TARGET_FILE:cmeta_plugin_linker_fixture_count>\""
  FOLDER "plugin/tests")
add_dependencies(cmeta_plugin_linker_test cmeta_plugin_linker_fixture_c
  cmeta_plugin_linker_fixture_cpp cmeta_plugin_linker_fixture_missing cmeta_plugin_linker_fixture_count)
set_target_properties(cmeta_plugin_linker_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

cmake_add_test(cmeta_plugin_scope_cpp_test
  SOURCES "${plugin_linker_test_dir}/plugin_scope_cpp_test.cpp"
  LIBS Salts::Plugin Salts::TinyTest
  DEFS "PLUGIN_SCOPE_PATH=\"$<TARGET_FILE:cmeta_plugin_linker_fixture_cpp>\""
  FOLDER "plugin/tests")
add_dependencies(cmeta_plugin_scope_cpp_test cmeta_plugin_linker_fixture_cpp)
set_target_properties(cmeta_plugin_scope_cpp_test PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
set_tests_properties(cmeta_plugin_linker_test cmeta_plugin_scope_cpp_test PROPERTIES TIMEOUT 60)

cmake_add_test(cmeta_plugin_cleanup_fatal_test
  SOURCES "${plugin_linker_test_dir}/plugin_cleanup_fatal_test.c"
  LIBS Salts::Plugin FOLDER "plugin/tests")
cmake_add_test(cmeta_plugin_scope_fatal_cpp_test
  SOURCES "${plugin_linker_test_dir}/plugin_scope_fatal_cpp_test.cpp"
  LIBS Salts::Plugin
  DEFS "PLUGIN_SCOPE_PATH=\"$<TARGET_FILE:cmeta_plugin_linker_fixture_cpp>\""
  FOLDER "plugin/tests")
add_dependencies(cmeta_plugin_scope_fatal_cpp_test cmeta_plugin_linker_fixture_cpp)
set_target_properties(cmeta_plugin_cleanup_fatal_test cmeta_plugin_scope_fatal_cpp_test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
set_tests_properties(cmeta_plugin_cleanup_fatal_test cmeta_plugin_scope_fatal_cpp_test
  PROPERTIES TIMEOUT 30)
