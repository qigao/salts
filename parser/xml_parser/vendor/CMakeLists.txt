set(SALTS_CXML_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/cxml)

set(SALTS_CXML_SOURCES
  ${SALTS_CXML_ROOT}/src/core/cxconfig.c
  ${SALTS_CXML_ROOT}/src/core/cxdefs.c
  ${SALTS_CXML_ROOT}/src/core/cxgrptable.c
  ${SALTS_CXML_ROOT}/src/core/cxlist.c
  ${SALTS_CXML_ROOT}/src/core/cxliteral.c
  ${SALTS_CXML_ROOT}/src/core/cxlrucache.c
  ${SALTS_CXML_ROOT}/src/core/cxmem.c
  ${SALTS_CXML_ROOT}/src/core/cxmset.c
  ${SALTS_CXML_ROOT}/src/core/cxstack.c
  ${SALTS_CXML_ROOT}/src/core/cxstr.c
  ${SALTS_CXML_ROOT}/src/core/cxtable.c
  ${SALTS_CXML_ROOT}/src/cxml/cxml.c
  ${SALTS_CXML_ROOT}/src/query/cxqapi.c
  ${SALTS_CXML_ROOT}/src/query/cxql.c
  ${SALTS_CXML_ROOT}/src/utils/cxutf8hook.c
  ${SALTS_CXML_ROOT}/src/utils/cxutils.c
  ${SALTS_CXML_ROOT}/src/xml/cxlexer.c
  ${SALTS_CXML_ROOT}/src/xml/cxparser.c
  ${SALTS_CXML_ROOT}/src/xml/cxprinter.c
  ${SALTS_CXML_ROOT}/src/xml/cxscope.c
  ${SALTS_CXML_ROOT}/src/xml/cxstream.c
  ${SALTS_CXML_ROOT}/src/xpath/cxxpcontext.c
  ${SALTS_CXML_ROOT}/src/xpath/cxxpdata.c
  ${SALTS_CXML_ROOT}/src/xpath/cxxpeval.c
  ${SALTS_CXML_ROOT}/src/xpath/cxxplexer.c
  ${SALTS_CXML_ROOT}/src/xpath/cxxplib.c
  ${SALTS_CXML_ROOT}/src/xpath/cxxpmemdeb.c
  ${SALTS_CXML_ROOT}/src/xpath/cxxpopt.c
  ${SALTS_CXML_ROOT}/src/xpath/cxxpparser.c
  ${SALTS_CXML_ROOT}/src/xpath/cxxpresolver.c)

add_library(salts_cxml OBJECT ${SALTS_CXML_SOURCES})
target_compile_features(salts_cxml PRIVATE c_std_11)
target_include_directories(salts_cxml PRIVATE
  ${SALTS_CXML_ROOT}/include)
target_link_libraries(salts_cxml PRIVATE
  Salts::Core Salts::Platform Salts::QueryVM)
target_compile_definitions(salts_cxml PRIVATE
  CXML_USE_QUERY_MOD CXML_USE_XPATH_MOD)

if(MSVC)
  target_compile_options(salts_cxml PRIVATE /W4 /wd4100 /wd4244 /wd4267)
else()
  target_compile_options(salts_cxml PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-sign-conversion)
endif()

cmake_config_target(salts_cxml
  NO_INSTALL
  FOLDER "parser/xml_parser/vendor")
