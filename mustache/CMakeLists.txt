set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG")

# =============================================================================
# Library
# =============================================================================
cmake_add_source(HEADER_FILES RECURSE DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
                 PATTERNS "*.h")
cmake_add_source(SOURCE_FILES RECURSE DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
                 PATTERNS "*.c")
# Build as SHARED library (DLL)
add_library(mustache SHARED ${HEADER_FILES} ${SOURCE_FILES})
cmake_config_target(mustache
  ALIAS TurboUtils::Mustache
  FOLDER "mustache"
  EXPORT_NAME Mustache)

target_compile_definitions(mustache PRIVATE SHARED_CXX)

target_include_directories(
  mustache
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include> $<INSTALL_INTERFACE:include/mustache>
  PRIVATE ${CMAKE_SOURCE_DIR}/parser/internal/json_parser/include
          ${CMAKE_SOURCE_DIR}/utils/include
          ${CMAKE_SOURCE_DIR}/vendor/cxml/include
          ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Link to json_parser and common
target_link_libraries(
  mustache
  PRIVATE TurboUtils::JsonParser cxml TurboUtils::SDS
  PUBLIC TurboUtils::Core)

# Example executable

add_executable(mustache_json_example examples/json_example.c)
target_link_libraries(mustache_json_example PRIVATE TurboUtils::Mustache
                                                    TurboUtils::JsonParser)
cmake_config_target(mustache_json_example NO_INSTALL FOLDER "mustache/examples")

add_executable(mustache_xml_example examples/xml_example.c)
target_link_libraries(mustache_xml_example PRIVATE TurboUtils::Mustache cxml)
cmake_config_target(mustache_xml_example NO_INSTALL FOLDER "mustache/examples")

add_subdirectory(test)
if(BUILD_BENCHMARKS OR BUILD_TESTS)
  add_subdirectory(benchmarks)
endif()

install(FILES include/mustache.h include/mustache_json.h include/mustache_xml.h
              DESTINATION include/mustache)
