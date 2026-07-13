# =============================================================================
# Executable
# =============================================================================

cmake_add_executable(tbe_compiler
  SOURCES main.c compiler_core.c mustache_helpers.c
  LIBS TurboUtils::TbeSchema TurboUtils::DataBind TurboUtils::Parser
       TurboUtils::Mustache
  INCLUDES ${PROJECT_SOURCE_DIR}/shared/utils/include
  FOLDER "tbe/tbe_compiler")

function(tbe_copy_turbonet_runtime_deps target_name)
  if(WIN32)
    set(runtime_deps
        $<TARGET_FILE:TurboUtils::Parser>
        $<TARGET_FILE:TurboUtils::Mustache>
        $<TARGET_FILE:TurboUtils::Core>)
    if(TARGET aklomp::base64)
      list(APPEND runtime_deps $<TARGET_FILE:aklomp::base64>)
    endif()

    add_custom_command(
      TARGET ${target_name}
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
        ${runtime_deps}
        $<TARGET_FILE_DIR:${target_name}>
      COMMENT "Copying TurboNet runtime DLLs for ${target_name}"
      VERBATIM)
  endif()
endfunction()

tbe_copy_turbonet_runtime_deps(tbe_compiler)

# =============================================================================
# Copy templates next to executable for easy access
# =============================================================================

add_custom_command(
  TARGET tbe_compiler
  POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/templates
    $<TARGET_FILE_DIR:tbe_compiler>/templates
  COMMENT "Copying template files to output directory")

# =============================================================================
# Tests
# =============================================================================

cmake_add_executable(test_tbe_compiler
  SOURCES test_tbe_compiler.c compiler_core.c mustache_helpers.c
  LIBS TurboUtils::TbeSchema TurboUtils::DataBind TurboUtils::TinyTest
       TurboUtils::Mustache
  DEFS
    SCHEMA_EXAMPLE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/example.schema"
    C_STRUCT_TEMPLATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/templates/c_structs.mustache"
    CPP_TYPES_TEMPLATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/templates/cpp_types.mustache"
    GO_TYPES_TEMPLATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/templates/go_types.mustache"
    PYTHON_DATACLASS_TEMPLATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/templates/python_dataclass.mustache"
    RUST_STRUCTS_TEMPLATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/templates/rust_structs.mustache"
    TS_TYPES_TEMPLATE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/templates/ts_types.mustache"
  FOLDER "tbe/tbe_compiler")
tbe_copy_turbonet_runtime_deps(test_tbe_compiler)
