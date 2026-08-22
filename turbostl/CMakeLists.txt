set(TARGET_NAME turbo_stl)

add_library(${TARGET_NAME}
  src/associative_meta.c
  src/instance_meta.c
  src/bplus_tree.c
  src/btree.c
  src/deque.c
  src/hash_map.c
  src/hash_set.c
  src/heap.c
  src/list.c
  src/map.c
  src/multimap.c
  src/rbtree.c
  src/sort.c
  src/set.c
  src/vec.c)

cmake_config_target(
  ${TARGET_NAME}
  ALIAS TurboUtils::STL
  FOLDER "turbostl"
  EXPORT_NAME STL)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(${TARGET_NAME} PUBLIC TurboUtils::CMeta)

add_library(turbo_stl_stream INTERFACE)
cmake_config_target(
  turbo_stl_stream
  ALIAS TurboUtils::STLStream
  FOLDER "turbostl"
  EXPORT_NAME STLStream)
target_link_libraries(
  turbo_stl_stream
  INTERFACE TurboUtils::STL TurboUtils::CFlow)

install(
  TARGETS ${TARGET_NAME}
  EXPORT TurboUtilsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  TARGETS turbo_stl_stream
  EXPORT TurboUtilsTargets)

install(
  DIRECTORY include/turbostl
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

install(
  FILES include/turbostl.h
  DESTINATION include)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
