set(TURBOUTILS_CXML_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/cxml)

set(TURBOUTILS_CXML_SOURCES
  ${TURBOUTILS_CXML_ROOT}/src/core/cxconfig.c
  ${TURBOUTILS_CXML_ROOT}/src/core/cxdefs.c
  ${TURBOUTILS_CXML_ROOT}/src/core/cxgrptable.c
  ${TURBOUTILS_CXML_ROOT}/src/core/cxlist.c
  ${TURBOUTILS_CXML_ROOT}/src/core/cxliteral.c
  ${TURBOUTILS_CXML_ROOT}/src/core/cxlrucache.c
  ${TURBOUTILS_CXML_ROOT}/src/core/cxmem.c
  ${TURBOUTILS_CXML_ROOT}/src/core/cxmset.c
  ${TURBOUTILS_CXML_ROOT}/src/core/cxstack.c
  ${TURBOUTILS_CXML_ROOT}/src/core/cxstr.c
  ${TURBOUTILS_CXML_ROOT}/src/core/cxtable.c
  ${TURBOUTILS_CXML_ROOT}/src/cxml/cxml.c
  ${TURBOUTILS_CXML_ROOT}/src/query/cxqapi.c
  ${TURBOUTILS_CXML_ROOT}/src/query/cxql.c
  ${TURBOUTILS_CXML_ROOT}/src/utils/cxutf8hook.c
  ${TURBOUTILS_CXML_ROOT}/src/utils/cxutils.c
  ${TURBOUTILS_CXML_ROOT}/src/xml/cxlexer.c
  ${TURBOUTILS_CXML_ROOT}/src/xml/cxparser.c
  ${TURBOUTILS_CXML_ROOT}/src/xml/cxprinter.c
  ${TURBOUTILS_CXML_ROOT}/src/xml/cxscope.c
  ${TURBOUTILS_CXML_ROOT}/src/xml/cxstream.c
  ${TURBOUTILS_CXML_ROOT}/src/xpath/cxxpcontext.c
  ${TURBOUTILS_CXML_ROOT}/src/xpath/cxxpdata.c
  ${TURBOUTILS_CXML_ROOT}/src/xpath/cxxpeval.c
  ${TURBOUTILS_CXML_ROOT}/src/xpath/cxxplexer.c
  ${TURBOUTILS_CXML_ROOT}/src/xpath/cxxplib.c
  ${TURBOUTILS_CXML_ROOT}/src/xpath/cxxpmemdeb.c
  ${TURBOUTILS_CXML_ROOT}/src/xpath/cxxpopt.c
  ${TURBOUTILS_CXML_ROOT}/src/xpath/cxxpparser.c
  ${TURBOUTILS_CXML_ROOT}/src/xpath/cxxpresolver.c)

add_library(turboutils_cxml OBJECT ${TURBOUTILS_CXML_SOURCES})
target_compile_features(turboutils_cxml PRIVATE c_std_11)
target_include_directories(turboutils_cxml PRIVATE
  ${TURBOUTILS_CXML_ROOT}/include)
target_link_libraries(turboutils_cxml PRIVATE
  TurboUtils::Core TurboUtils::Platform TurboUtils::QueryVM)
target_compile_definitions(turboutils_cxml PRIVATE
  CXML_USE_QUERY_MOD CXML_USE_XPATH_MOD)

if(MSVC)
  target_compile_options(turboutils_cxml PRIVATE /W4 /wd4100 /wd4244 /wd4267)
else()
  target_compile_options(turboutils_cxml PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-sign-conversion)
endif()

cmake_config_target(turboutils_cxml
  NO_INSTALL
  FOLDER "parser/xml_parser/vendor")
