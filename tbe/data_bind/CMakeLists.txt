# =============================================================================
# Library
# =============================================================================

set(TARGET_NAME data_bind)

add_library(${TARGET_NAME} SHARED data_bind.c data_bind.h re.c re.h tbe_helpers.c)



target_compile_definitions(${TARGET_NAME} PRIVATE DATA_BIND_BUILD_DLL)
if(WIN32)
  target_compile_definitions(${TARGET_NAME}
    INTERFACE DATA_BIND_USE_DLL)
endif()

target_include_directories(${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
         $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tbe/schema/include
    ${CMAKE_SOURCE_DIR}/vendor/mir
)

target_link_libraries(${TARGET_NAME}
  PRIVATE
    TurboUtils::TbeSchema
    mir_static
    uuid
  PUBLIC
    TurboUtils::Core
    TurboUtils::Parser
)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS TurboUtils::DataBind
  FOLDER tbe/data_bind
  EXPORT_NAME DataBind)

install(
  TARGETS ${TARGET_NAME}
  EXPORT TurboUtilsTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES data_bind.h DESTINATION include)

# =============================================================================
# Tests
# =============================================================================

cmake_add_test(test_data_bind
  SOURCES test_data_bind.c tbe_helpers.c
  LIBS TurboUtils::DataBind TurboUtils::TinyTest uuid
  INCLUDES ${CMAKE_SOURCE_DIR}/tbe/schema/include
  FOLDER "tbe/data_bind")

cmake_add_test(test_data_bind_public_api
  SOURCES test_data_bind_public_api.c
  LIBS TurboUtils::DataBind TurboUtils::TinyTest
  FOLDER "tbe/data_bind")

cmake_add_test(test_re
  SOURCES test_re.c re.c
  LIBS TurboUtils::TinyTest
  FOLDER "tbe/data_bind")
