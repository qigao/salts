set(TARGET_NAME salts_simd)

# SIMDe is supplied by the repository's vcpkg contract and warmed by
# qigao/vcpkg-cache. It remains a private implementation dependency.
find_path(SIMDE_INCLUDE_DIRS "simde/wasm/simd128.h" REQUIRED)

add_library(${TARGET_NAME}
  src/simd.c)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS Salts::SIMD
  FOLDER "simd"
  EXPORT_NAME SIMD)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE $<BUILD_INTERFACE:${SIMDE_INCLUDE_DIRS}>)
target_link_libraries(${TARGET_NAME} PUBLIC Salts::CMeta)

# Some SIMDe scalar fallbacks (for example nearest) use libm symbols such as
# nearbyint/nearbyintf. Keep that runtime dependency in the exported static
# link contract on platforms where libm is separate.
if(NOT WIN32)
  target_link_libraries(${TARGET_NAME} PUBLIC m)
endif()

install(
  TARGETS ${TARGET_NAME}
  EXPORT SaltsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  DIRECTORY include/salts
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
