set(STREAM_EXAMPLE_SOURCES
  advanced_strings_example.c
  example.c
  java_style_example.c
  container_example.c
  turbo_container_init_example.c
  turbo_multimap_example.c
  turbo_partition_example.c
  turbo_heap_example.c
  typed_example.c
  streamable_example.c
  temporal_example.c
  turbo_containers_example.c
  group_count_example.c
  turbo_collect_grouping_example.c
  turbo_partition_count_example.c
  turbo_partition_reduce_example.c)

foreach(example_source IN LISTS STREAM_EXAMPLE_SOURCES)
  get_filename_component(example_name ${example_source} NAME_WE)
  set(example_target stream_${example_name})

  add_executable(${example_target} ${example_source})
  target_link_libraries(${example_target} PRIVATE TurboUtils::Stream)
  cmake_config_target(${example_target}
    NO_INSTALL
    FOLDER "stream/examples")
endforeach()
