add_library(turbo_cflow_minicoro STATIC src/minicoro.c)

cmake_config_target(
  turbo_cflow_minicoro
  ALIAS TurboUtils::CFlowMinicoro
  FOLDER "cflow/minicoro"
  EXPORT_NAME CFlowMinicoro)

target_compile_features(turbo_cflow_minicoro PUBLIC c_std_11)
target_include_directories(
  turbo_cflow_minicoro
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${PROJECT_SOURCE_DIR}/vendor/minicoro)
target_link_libraries(turbo_cflow_minicoro PUBLIC TurboUtils::CFlow)

install(
  TARGETS turbo_cflow_minicoro
  EXPORT TurboUtilsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  DIRECTORY include/cflow
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  cmake_add_test(cflow_minicoro_test
    SOURCES tests/cflow_minicoro_test.c
    LIBS TurboUtils::CFlowMinicoro TurboUtils::TinyTest
    FOLDER "cflow/minicoro/tests")
  set_target_properties(cflow_minicoro_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
endif()
