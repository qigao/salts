set(TARGET_NAME turbo_utils)
# Add parser subdirectory first (generates lexer)
add_subdirectory(parser)
cmake_add_source(SOURCE_FILES RECURSE)
find_path(SIMDE_INCLUDE_DIRS "simde/x86/sse2.h" REQUIRED)

add_library(${TARGET_NAME} SHARED ${SOURCE_FILES})
# Configure target
cmake_config_target(
  ${TARGET_NAME}
  ALIAS
  TurboUtils::Core
  FOLDER
  "utils"
  EXPORT_NAME
  Core)

# Include directories
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/parser>
         $<INSTALL_INTERFACE:include>
  PRIVATE $<BUILD_INTERFACE:${SIMDE_INCLUDE_DIRS}>
          $<BUILD_INTERFACE:${FMT_LEXER_INCLUDE_DIRS}>
          $<BUILD_INTERFACE:${LOG_PATTERN_LEXER_INCLUDE_DIRS}>)

target_compile_definitions(
  ${TARGET_NAME}
  PUBLIC
    $<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>,$<CONFIG:RelWithDebInfo>>:TURBO_LOG_CAPTURE_SOURCE=0>
  PRIVATE SHARED_CXX)
target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_compile_options(${TARGET_NAME}
  PUBLIC $<$<COMPILE_LANG_AND_ID:C,MSVC>:/experimental:c11atomics>)
if(UNIX)
  target_compile_definitions(
    ${TARGET_NAME} PUBLIC _DEFAULT_SOURCE=1 _POSIX_C_SOURCE=200809L
                          _XOPEN_SOURCE=700)
endif()

# Link dependencies
target_link_libraries(
  ${TARGET_NAME}
  PRIVATE ${FMT_LEXER_LIBRARY} TurboUtils::SDS # Simple Dynamic Strings
          aklomp::base64 ${LOG_PATTERN_LEXER_LIBRARY} # Log pattern lexer
)

if(WIN32)
  target_link_libraries(${TARGET_NAME} PRIVATE iphlpapi ws2_32)
endif()

install(
  TARGETS ${TARGET_NAME}
  EXPORT TurboUtilsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Install headers
install(
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
  DESTINATION include
  FILES_MATCHING
  PATTERN "*.h")

# Install extra parser header
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/parser/fmt_lexer.h
        DESTINATION include)
# Tests
add_subdirectory(tests)
if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
add_subdirectory(examples)
