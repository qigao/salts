if(NOT (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
        ((APPLE OR CMAKE_SYSTEM_NAME MATCHES "^(FreeBSD|OpenBSD|NetBSD|DragonFly)$")
         AND CMAKE_SIZEOF_VOID_P EQUAL 8)))
  return()
endif()

cmake_add_benchmark(native_io_pipe_benchmark
  SOURCES native_io_pipe_benchmark.c
  LIBS Salts::NativeIO Salts::TinyTest
  FOLDER "native-io/benchmarks")
set_target_properties(native_io_pipe_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)


cmake_add_benchmark(native_io_sharded_routing_benchmark
  SOURCES native_io_sharded_routing_benchmark.c
  LIBS Salts::NativeIO Salts::Platform
  FOLDER "native-io/benchmarks")
set_target_properties(native_io_sharded_routing_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)


cmake_add_benchmark(native_io_pipe_styles_benchmark
  SOURCES native_io_pipe_styles_benchmark.c
  LIBS Salts::NativeIO Salts::Platform
  FOLDER "native-io/benchmarks")
set_target_properties(native_io_pipe_styles_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)


cmake_add_benchmark(native_io_stream_styles_benchmark
  SOURCES native_io_stream_styles_benchmark.c
  LIBS Salts::NativeIO Salts::Platform
  FOLDER "native-io/benchmarks")
set_target_properties(native_io_stream_styles_benchmark PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
if(WIN32)
  target_link_libraries(native_io_stream_styles_benchmark PRIVATE ws2_32)
endif()


if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cmake_add_benchmark(native_io_queue_depth_benchmark
    SOURCES native_io_queue_depth_benchmark.c
    LIBS Salts::NativeIO Salts::Platform
    FOLDER "native-io/benchmarks")
  set_target_properties(native_io_queue_depth_benchmark PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
endif()


if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  cmake_add_benchmark(native_io_idle_observe_benchmark
    SOURCES native_io_idle_observe_benchmark.c
    LIBS Salts::NativeIO Salts::Platform
    FOLDER "native-io/benchmarks")
  set_target_properties(native_io_idle_observe_benchmark PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
endif()
