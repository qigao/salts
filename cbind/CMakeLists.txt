set(TARGET_NAME turbo_cbind)

add_library(${TARGET_NAME}
  src/decode.c)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS TurboUtils::CBind
  FOLDER "cbind"
  EXPORT_NAME CBind)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(${TARGET_NAME}
  PUBLIC TurboUtils::CMeta TurboUtils::CSerde)

install(
  TARGETS ${TARGET_NAME}
  EXPORT TurboUtilsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  DIRECTORY include/cbind
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
