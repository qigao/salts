set(TARGET_NAME turbo_container)

add_library(${TARGET_NAME}
  src/btree.c
  src/deque.c
  src/hash_map.c
  src/heap.c
  src/set.c
  src/vec.c)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS TurboUtils::Container
  FOLDER "container"
  EXPORT_NAME Container)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)

# Raw container headers use the repository's platform/error support.  They do
# not depend on CMeta or CFlow; typed integration lives in container/typed.h.
target_link_libraries(${TARGET_NAME} PUBLIC TurboUtils::Core)

install(
  TARGETS ${TARGET_NAME}
  EXPORT TurboUtilsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  DIRECTORY include/container
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")
