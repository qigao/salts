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
        $<TARGET_FILE:TurboUtils::DataBind>
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

cmake_add_test(test_tbe_compiler
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
    TBE_COMPILER_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  FOLDER "tbe/tbe_compiler")
tbe_copy_turbonet_runtime_deps(test_tbe_compiler)

set(TBE_TYPED_ORDER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(TBE_TYPED_ORDER_HEADER "${TBE_TYPED_ORDER_GENERATED_DIR}/typed_order.h")
set(TBE_TYPED_ORDER_SOURCE "${TBE_TYPED_ORDER_GENERATED_DIR}/typed_order.c")
set(TBE_TYPED_ORDER_GUEST_SOURCE "${TBE_TYPED_ORDER_GENERATED_DIR}/typed_order_guest.c")

add_custom_command(
  OUTPUT
    "${TBE_TYPED_ORDER_HEADER}"
    "${TBE_TYPED_ORDER_SOURCE}"
    "${TBE_TYPED_ORDER_GUEST_SOURCE}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${TBE_TYPED_ORDER_GENERATED_DIR}"
  COMMAND
    $<TARGET_FILE:tbe_compiler>
    "${CMAKE_CURRENT_SOURCE_DIR}/test_typed_order.schema"
    --lang c
    --output "${TBE_TYPED_ORDER_HEADER}"
    --source-output "${TBE_TYPED_ORDER_SOURCE}"
    --guest-output "${TBE_TYPED_ORDER_GUEST_SOURCE}"
  DEPENDS
    tbe_compiler
    "${CMAKE_CURRENT_SOURCE_DIR}/test_typed_order.schema"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_structs.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_typed_source.mustache"
    "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_guest_adapter.mustache"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "Generating strong typed Order C bindings"
  VERBATIM)

cmake_add_test(test_tbe_typed_order
  SOURCES
    test_typed_order.c
    "${TBE_TYPED_ORDER_SOURCE}"
    "${TBE_TYPED_ORDER_GUEST_SOURCE}"
  LIBS TurboUtils::DataBind TurboUtils::TinyTest
  INCLUDES "${TBE_TYPED_ORDER_GENERATED_DIR}"
  FOLDER "tbe/tbe_compiler")
tbe_copy_turbonet_runtime_deps(test_tbe_typed_order)

cmake_add_test(test_tbe_typed_order_cpp
  SOURCES
    test_typed_order_cpp.cpp
    "${TBE_TYPED_ORDER_SOURCE}"
  LIBS TurboUtils::DataBind TurboUtils::TinyTest
  INCLUDES "${TBE_TYPED_ORDER_GENERATED_DIR}"
  FOLDER "tbe/tbe_compiler")
set_target_properties(test_tbe_typed_order_cpp PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON)
tbe_copy_turbonet_runtime_deps(test_tbe_typed_order_cpp)

find_program(TBE_WASM_GUEST_CLANG NAMES clang clang.exe)
if(TBE_WASM_GUEST_CLANG)
  set(TBE_WASM_ORDER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_wasm")
  set(TBE_WASM_ORDER_HEADER "${TBE_WASM_ORDER_GENERATED_DIR}/wasm_order.h")
  set(TBE_WASM_ORDER_SOURCE "${TBE_WASM_ORDER_GENERATED_DIR}/wasm_order_guest.c")
  set(TBE_WASM_ORDER_OBJECT "${TBE_WASM_ORDER_GENERATED_DIR}/wasm_order_guest.o")

  add_custom_command(
    OUTPUT "${TBE_WASM_ORDER_HEADER}" "${TBE_WASM_ORDER_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TBE_WASM_ORDER_GENERATED_DIR}"
    COMMAND
      $<TARGET_FILE:tbe_compiler>
      "${CMAKE_CURRENT_SOURCE_DIR}/test_typed_order.schema"
      --lang c
      --output "${TBE_WASM_ORDER_HEADER}"
      --guest-output "${TBE_WASM_ORDER_SOURCE}"
    DEPENDS
      tbe_compiler
      "${CMAKE_CURRENT_SOURCE_DIR}/test_typed_order.schema"
      "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_structs.mustache"
      "${CMAKE_CURRENT_SOURCE_DIR}/templates/c_guest_adapter.mustache"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating freestanding Wasm Order guest bindings"
    VERBATIM)

  add_custom_command(
    OUTPUT "${TBE_WASM_ORDER_OBJECT}"
    COMMAND
      "${TBE_WASM_GUEST_CLANG}"
      --target=wasm32-unknown-unknown
      -DTBE_WASM_GUEST=1
      -O2
      -nostdlib
      -Wall
      -Wextra
      -Werror
      -c "${TBE_WASM_ORDER_SOURCE}"
      -I "${TBE_WASM_ORDER_GENERATED_DIR}"
      -I "${CMAKE_SOURCE_DIR}/tbe/schema/include"
      -o "${TBE_WASM_ORDER_OBJECT}"
    DEPENDS "${TBE_WASM_ORDER_HEADER}" "${TBE_WASM_ORDER_SOURCE}"
    COMMENT "Compiling generated Order adapter for wasm32-unknown-unknown"
    VERBATIM)

  add_custom_target(test_tbe_wasm_guest_compile DEPENDS "${TBE_WASM_ORDER_OBJECT}")
  add_dependencies(test_tbe_typed_order test_tbe_wasm_guest_compile)
  set_target_properties(test_tbe_wasm_guest_compile PROPERTIES FOLDER "tbe/tbe_compiler")
else()
  message(STATUS "clang not found; freestanding Wasm guest compile check is disabled")
endif()
