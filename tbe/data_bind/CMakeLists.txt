# =============================================================================
# Library
# =============================================================================

set(TARGET_NAME data_bind)

add_library(${TARGET_NAME} SHARED data_bind.c data_bind.h data_bind_record.c tbe_typed.c
                                  tbe_typed.h)



target_compile_definitions(${TARGET_NAME} PRIVATE DATA_BIND_BUILD_DLL)
if(WIN32)
  target_compile_definitions(${TARGET_NAME}
    INTERFACE DATA_BIND_USE_DLL)
endif()

target_include_directories(${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
         $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/tbe/schema/include>
         $<INSTALL_INTERFACE:include>
)

target_link_libraries(${TARGET_NAME}
  PRIVATE
    TurboUtils::TbeSchema
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
install(FILES data_bind.h tbe_typed.h DESTINATION include)

# =============================================================================
# Tests
# =============================================================================

cmake_add_test(test_data_bind
  SOURCES test_data_bind.c
  LIBS TurboUtils::DataBind TurboUtils::TinyTest
  INCLUDES ${CMAKE_SOURCE_DIR}/tbe/schema/include
  FOLDER "tbe/data_bind")

cmake_add_test(test_data_bind_public_api
  SOURCES test_data_bind_public_api.c
  LIBS TurboUtils::DataBind TurboUtils::TinyTest
  FOLDER "tbe/data_bind")

cmake_add_test(test_data_bind_record
  SOURCES test_data_bind_record.c
  LIBS TurboUtils::DataBind TurboUtils::TinyTest
  FOLDER "tbe/data_bind")

cmake_add_test(test_data_bind_pool
  SOURCES test_data_bind_pool.c
  LIBS TurboUtils::DataBind TurboUtils::TinyTest
  FOLDER "tbe/data_bind")

cmake_add_test(test_tbe_typed
  SOURCES test_tbe_typed.c
  LIBS TurboUtils::DataBind TurboUtils::TinyTest
  FOLDER "tbe/data_bind")

cmake_add_benchmark(benchmark_data_bind_pool
  SOURCES benchmark_data_bind_pool.c
  LIBS TurboUtils::DataBind TurboUtils::TinyTest
  FOLDER "tbe/data_bind")

cmake_add_benchmark(benchmark_data_bind_json_path
  SOURCES benchmark_data_bind_json_path.c
  LIBS TurboUtils::DataBind TurboUtils::TinyTest
  FOLDER "tbe/data_bind")
