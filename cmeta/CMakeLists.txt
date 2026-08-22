set(TARGET_NAME turbo_cmeta)

add_library(${TARGET_NAME}
  src/cmeta.c
  src/entry.c
  src/type_identity.c)

cmake_config_target(${TARGET_NAME}
  ALIAS TurboUtils::CMeta
  FOLDER "cmeta"
  EXPORT_NAME CMeta)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)

target_include_directories(${TARGET_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if(BUILD_SHARED_LIBS)
  if(WIN32)
    target_compile_definitions(${TARGET_NAME}
      PRIVATE CMETA_BUILD_SHARED CMETA_EXPORTS
      INTERFACE CMETA_BUILD_SHARED)
  else()
    set_target_properties(${TARGET_NAME} PROPERTIES
      C_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN YES)
    target_compile_definitions(${TARGET_NAME} PRIVATE CMETA_BUILD_SHARED)
  endif()
endif()

if(TURBO_UTILS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
