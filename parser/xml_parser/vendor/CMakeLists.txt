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
  ${TURBOUTILS_CXML_ROOT}/src/utils/cxutf8hook.c
  ${TURBOUTILS_CXML_ROOT}/src/utils/cxutils.c
  ${TURBOUTILS_CXML_ROOT}/src/xml/cxlexer.c
  ${TURBOUTILS_CXML_ROOT}/src/xml/cxparser.c
  ${TURBOUTILS_CXML_ROOT}/src/xml/cxprinter.c
  ${TURBOUTILS_CXML_ROOT}/src/xml/cxscope.c
  ${TURBOUTILS_CXML_ROOT}/src/xml/cxstream.c)

add_library(turboutils_cxml OBJECT ${TURBOUTILS_CXML_SOURCES})
target_compile_features(turboutils_cxml PRIVATE c_std_11)
target_include_directories(turboutils_cxml PRIVATE
  ${TURBOUTILS_CXML_ROOT}/include)

if(MSVC)
  target_compile_options(turboutils_cxml PRIVATE /W4 /wd4100 /wd4244 /wd4267)
else()
  target_compile_options(turboutils_cxml PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-sign-conversion)
endif()

cmake_config_target(turboutils_cxml
  NO_INSTALL
  FOLDER "parser/xml_parser/vendor")
