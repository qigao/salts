add_library(turbo_parser SHARED src/turbo_parser.c)
cmake_config_target(turbo_parser
  ALIAS TurboUtils::Parser
  EXPORT_NAME Parser
  FOLDER turbo_parser)

target_include_directories(
  turbo_parser
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
          $<INSTALL_INTERFACE:include>
  PRIVATE ${PROJECT_SOURCE_DIR}/parser/dotenv_parser/src
  )


target_link_libraries(
  turbo_parser
  PUBLIC TurboUtils::Core
   PRIVATE csv_parser
          ini_parser
          json_parser
          uri_parser
          tlv_parser
          ltv_parser
          modbus_parser
          soa_parser
          dotenv_parser
          cmd_parser
          toon
          toml_parser
          datetime_parser
          cyaml_json_adapter
          cxml)

target_compile_definitions(turbo_parser PRIVATE SHARED_CXX)

install(
  TARGETS turbo_parser
  EXPORT TurboUtilsTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/turbo_parser.h
        DESTINATION include)

# Tests
cmake_add_test(test_turbo_parser
  SOURCES test/test_turbo_parser.c
  LIBS turbo_parser TurboUtils::TinyTest
  FOLDER turbo_parser)
cmake_add_test(test_turbo_parser_bdd
  SOURCES test/test_turbo_parser_bdd.c
  LIBS turbo_parser TurboUtils::TinyTest
  FOLDER turbo_parser)
