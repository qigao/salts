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
    ${CMAKE_SOURCE_DIR}/tbe/tbe/include
    ${CMAKE_SOURCE_DIR}/vendor/mir
)

target_link_libraries(${TARGET_NAME}
  PRIVATE
    TurboUtils::SchemaBE
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

install(FILES data_bind.h DESTINATION include)

# =============================================================================
# Tests
# =============================================================================

add_executable(test_data_bind test_data_bind.c tbe_helpers.c)
target_include_directories(test_data_bind
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tbe/tbe/include
)
target_link_libraries(test_data_bind
  PRIVATE
    TurboUtils::DataBind
    TurboUtils::TinyTest
    uuid
)
cmake_config_target(test_data_bind NO_INSTALL FOLDER "tbe/data_bind")
add_test(NAME test_data_bind COMMAND test_data_bind)

add_executable(test_data_bind_public_api test_data_bind_public_api.c)
target_link_libraries(test_data_bind_public_api
  PRIVATE
    TurboUtils::DataBind
    TurboUtils::TinyTest
)
cmake_config_target(test_data_bind_public_api NO_INSTALL FOLDER "tbe/data_bind")
add_test(NAME test_data_bind_public_api COMMAND test_data_bind_public_api)

add_executable(test_re test_re.c re.c)
target_link_libraries(test_re
  PRIVATE
    TurboUtils::TinyTest
)
cmake_config_target(test_re NO_INSTALL FOLDER "tbe/data_bind")
add_test(NAME test_re COMMAND test_re)
