set(TARGET_NAME turbo_container)

add_library(${TARGET_NAME}
  src/turbo_bplus_tree.c
  src/turbo_btree.c
  src/turbo_deque.c
  src/turbo_hash_map.c
  src/turbo_hash_set.c
  src/turbo_heap.c
  src/turbo_list.c
  src/turbo_sort.c
  src/turbo_set.c
  src/turbo_vec.c)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS TurboUtils::Container
  FOLDER "container"
  EXPORT_NAME Container)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(${TARGET_NAME} PUBLIC TurboUtils::CMeta)

install(
  TARGETS ${TARGET_NAME}
  EXPORT TurboUtilsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  DIRECTORY include/turbo
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
