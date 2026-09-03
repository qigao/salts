set(TARGET_NAME salts_cstl)

add_library(${TARGET_NAME}
  src/associative_meta.c
  src/construction_meta.c
  src/generic_meta.c
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
  ALIAS Salts::CSTL
  FOLDER "cstl"
  EXPORT_NAME CSTL)

target_compile_features(${TARGET_NAME} PUBLIC c_std_11)
target_include_directories(
  ${TARGET_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(${TARGET_NAME} PUBLIC Salts::CMeta)

add_library(salts_cstl_stream
  src/cflow_backend.c)
cmake_config_target(
  salts_cstl_stream
  ALIAS Salts::CSTLStream
  FOLDER "cstl"
  EXPORT_NAME CSTLStream)
target_link_libraries(
  salts_cstl_stream
  PUBLIC Salts::CSTL Salts::CFlow)
target_compile_features(salts_cstl_stream PUBLIC c_std_11)

install(
  TARGETS ${TARGET_NAME}
  EXPORT SaltsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  TARGETS salts_cstl_stream
  EXPORT SaltsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(
  DIRECTORY include/cstl
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")

install(
  FILES include/cstl.h
  DESTINATION include)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
