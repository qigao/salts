set(ROCIDA_CXML_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/cxml)

set(ROCIDA_CXML_SOURCES
  ${ROCIDA_CXML_ROOT}/src/core/cxconfig.c
  ${ROCIDA_CXML_ROOT}/src/core/cxdefs.c
  ${ROCIDA_CXML_ROOT}/src/core/cxgrptable.c
  ${ROCIDA_CXML_ROOT}/src/core/cxlist.c
  ${ROCIDA_CXML_ROOT}/src/core/cxliteral.c
  ${ROCIDA_CXML_ROOT}/src/core/cxlrucache.c
  ${ROCIDA_CXML_ROOT}/src/core/cxmem.c
  ${ROCIDA_CXML_ROOT}/src/core/cxmset.c
  ${ROCIDA_CXML_ROOT}/src/core/cxstack.c
  ${ROCIDA_CXML_ROOT}/src/core/cxstr.c
  ${ROCIDA_CXML_ROOT}/src/core/cxtable.c
  ${ROCIDA_CXML_ROOT}/src/cxml/cxml.c
  ${ROCIDA_CXML_ROOT}/src/query/cxqapi.c
  ${ROCIDA_CXML_ROOT}/src/query/cxql.c
  ${ROCIDA_CXML_ROOT}/src/utils/cxutf8hook.c
  ${ROCIDA_CXML_ROOT}/src/utils/cxutils.c
  ${ROCIDA_CXML_ROOT}/src/xml/cxlexer.c
  ${ROCIDA_CXML_ROOT}/src/xml/cxparser.c
  ${ROCIDA_CXML_ROOT}/src/xml/cxprinter.c
  ${ROCIDA_CXML_ROOT}/src/xml/cxscope.c
  ${ROCIDA_CXML_ROOT}/src/xml/cxstream.c
  ${ROCIDA_CXML_ROOT}/src/xpath/cxxpcontext.c
  ${ROCIDA_CXML_ROOT}/src/xpath/cxxpdata.c
  ${ROCIDA_CXML_ROOT}/src/xpath/cxxpeval.c
  ${ROCIDA_CXML_ROOT}/src/xpath/cxxplexer.c
  ${ROCIDA_CXML_ROOT}/src/xpath/cxxplib.c
  ${ROCIDA_CXML_ROOT}/src/xpath/cxxpmemdeb.c
  ${ROCIDA_CXML_ROOT}/src/xpath/cxxpopt.c
  ${ROCIDA_CXML_ROOT}/src/xpath/cxxpparser.c
  ${ROCIDA_CXML_ROOT}/src/xpath/cxxpresolver.c)

add_library(rocida_cxml OBJECT ${ROCIDA_CXML_SOURCES})
target_compile_features(rocida_cxml PRIVATE c_std_11)
target_include_directories(rocida_cxml PRIVATE
  ${ROCIDA_CXML_ROOT}/include)
target_link_libraries(rocida_cxml PRIVATE
  Rocida::Core Rocida::Platform Rocida::QueryVM)
target_compile_definitions(rocida_cxml PRIVATE
  CXML_USE_QUERY_MOD CXML_USE_XPATH_MOD)

if(MSVC)
  target_compile_options(rocida_cxml PRIVATE /W4 /wd4100 /wd4244 /wd4267)
else()
  target_compile_options(rocida_cxml PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-sign-conversion)
endif()

cmake_config_target(rocida_cxml
  NO_INSTALL
  FOLDER "parser/xml_parser/vendor")
