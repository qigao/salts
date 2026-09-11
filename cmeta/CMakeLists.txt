set(TARGET_NAME salts_cmeta)

add_library(${TARGET_NAME}
  src/cmeta.c
  src/container_type.c
  src/data.c
  src/declared_type.c
  src/entry.c
  src/infer.c
  src/type_identity.c)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS Salts::CMeta
  FOLDER "cmeta"
  EXPORT_NAME CMeta)

# A static CMeta archive may be embedded by Core and multiple bridge DSOs.
# Keep each image's immutable descriptors and helpers local instead of exposing
# preemptible duplicate globals. Type compatibility is semantic, not address-based.
# Do not apply hidden visibility to an explicitly requested shared CMeta target.
get_target_property(cmeta_library_type ${TARGET_NAME} TYPE)
if(cmeta_library_type STREQUAL "STATIC_LIBRARY")
  set_target_properties(${TARGET_NAME} PROPERTIES C_VISIBILITY_PRESET hidden)
endif()

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_compile_options(${TARGET_NAME}
  PUBLIC $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/Zc:preprocessor>)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)

install(
  TARGETS ${TARGET_NAME}
  EXPORT SaltsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  DIRECTORY include/cmeta
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()