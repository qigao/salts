add_library(cflow_scxml STATIC src/scxml.c)
cmake_config_target(cflow_scxml
  ALIAS TurboUtils::CFlowScxml
  FOLDER "cflow-scxml"
  EXPORT_NAME CFlowScxml)
target_compile_features(cflow_scxml PUBLIC c_std_11)
target_include_directories(cflow_scxml
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(cflow_scxml
  PUBLIC TurboUtils::CFlow TurboUtils::XmlParser)

if(BUILD_TESTING)
  get_target_property(CFLOW_PUBLIC_LINKS turbo_cflow INTERFACE_LINK_LIBRARIES)
  if(CFLOW_PUBLIC_LINKS MATCHES "XmlParser|cxml|CSerde")
    message(FATAL_ERROR
      "TurboUtils::CFlow must remain independent of XML and CSerde")
  endif()
  get_target_property(CFLOW_SCXML_PUBLIC_LINKS
                      cflow_scxml INTERFACE_LINK_LIBRARIES)
  if(NOT CFLOW_SCXML_PUBLIC_LINKS STREQUAL
     "TurboUtils::CFlow;TurboUtils::XmlParser")
    message(FATAL_ERROR
      "TurboUtils::CFlowScxml dependency contract changed")
  endif()
endif()

install(
  TARGETS cflow_scxml
  EXPORT TurboUtilsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(
  DIRECTORY include/cflow
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
