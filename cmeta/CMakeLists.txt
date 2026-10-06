set(TARGET_NAME salts_cmeta)

add_library(${TARGET_NAME}
  src/abi.c
  src/cmeta.c
  src/container_type.c
  src/data_v2.c
  src/declared_type.c
  src/entry.c
  src/function.c
  src/fastpath.c
  src/trace.c
  src/fixed_array.c
  src/interface.c
  src/method.c
  src/object.c
  src/infer.c
  src/invokable.c
  src/type_identity.c
  src/vector.c)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS Salts::CMeta
  FOLDER "cmeta"
  EXPORT_NAME CMeta)

# A static CMeta archive may be embedded by Core and multiple bridge DSOs.
# Keep each image's immutable descriptors and helpers local instead of exposing
# preemptible duplicate globals. Type compatibility is semantic, not address-based.
# Do not apply hidden visibility to an explicitly requested shared CMeta target.
get_target_property(cmeta_library_type ${TARGET_NAME} TYPE)
if(cmeta_library_type STREQUAL "STATIC_LIBRARY")
  set_target_properties(${TARGET_NAME} PROPERTIES C_VISIBILITY_PRESET hidden)
  target_compile_definitions(${TARGET_NAME} PRIVATE
    "$<$<COMPILE_LANGUAGE:ASM>:CMETA_STATIC_LIBRARY=1>")
endif()

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
if(CMETA_NATIVE_FASTPATH)
  if(ENABLE_SANITIZER_THREAD)
    message(FATAL_ERROR "CMETA_NATIVE_FASTPATH assembly loads cannot be qualified by ThreadSanitizer; use the portable C reference")
  endif()
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "CMETA_NATIVE_FASTPATH requires a qualified 64-bit ABI")
  endif()
  set(cmeta_fastpath_processor "${CMAKE_SYSTEM_PROCESSOR}")
  if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES cmeta_architecture_count)
    if(NOT cmeta_architecture_count EQUAL 1)
      message(FATAL_ERROR "CMETA_NATIVE_FASTPATH requires one explicitly qualified macOS architecture")
    endif()
    set(cmeta_fastpath_processor "${CMAKE_OSX_ARCHITECTURES}")
  endif()
  if(cmeta_fastpath_processor MATCHES "^(AMD64|amd64|x86_64|X86_64)$")
    if(WIN32)
      enable_language(ASM_MASM)
      target_sources(${TARGET_NAME} PRIVATE arch/windows-x64/fastpath.asm)
    elseif(UNIX)
      enable_language(ASM)
      target_sources(${TARGET_NAME} PRIVATE arch/x86_64/fastpath.S)
    else()
      message(FATAL_ERROR "CMETA_NATIVE_FASTPATH has no qualified x86_64 OS backend")
    endif()
  elseif(cmeta_fastpath_processor MATCHES "^(aarch64|arm64|ARM64)$" AND UNIX)
    enable_language(ASM)
    target_sources(${TARGET_NAME} PRIVATE arch/aarch64/fastpath.S)
  else()
    message(FATAL_ERROR "CMETA_NATIVE_FASTPATH has no qualified backend for ${CMAKE_SYSTEM_NAME}/${cmeta_fastpath_processor}")
  endif()
  target_compile_definitions(${TARGET_NAME} PUBLIC CMETA_NATIVE_FASTPATH=1)
endif()
target_compile_options(${TARGET_NAME}
  PUBLIC $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/Zc:preprocessor>)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)

install(
  TARGETS ${TARGET_NAME}
  EXPORT SaltsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  DIRECTORY include/cmeta
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
if(BUILD_BENCHMARKS OR CMETA_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
