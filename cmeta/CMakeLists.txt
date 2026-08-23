set(TARGET_NAME turbo_cmeta)

add_library(${TARGET_NAME}
  src/cmeta.c
  src/container_type.c
  src/data.c
  src/entry.c
  src/type_identity.c)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS TurboUtils::CMeta
  FOLDER "cmeta"
  EXPORT_NAME CMeta)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_compile_options(${TARGET_NAME}
  PUBLIC $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/Zc:preprocessor>)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)

install(
  TARGETS ${TARGET_NAME}
  EXPORT TurboUtilsTargets
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
