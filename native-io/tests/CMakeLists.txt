cmake_add_test(native_io_test
  SOURCES native_io_test.c
  LIBS Salts::NativeIO Salts::Coroutine Salts::TinyTest Salts::Concurrency
  FOLDER "native-io/tests")
set_target_properties(native_io_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(native_io_uring_batch_test native_io_uring_batch_test.c)
  target_link_libraries(native_io_uring_batch_test PRIVATE Salts::NativeIO Salts::TinyTest)
  set_target_properties(native_io_uring_batch_test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF FOLDER "native-io/tests")
  add_test(NAME native_io_uring_batch_test COMMAND native_io_uring_batch_test)
  add_dependencies(native_io_test native_io_uring_batch_test)
endif()

if(WIN32)
  target_link_libraries(native_io_test PRIVATE ws2_32)
endif()

cmake_add_test(native_io_header_cpp_test
  SOURCES native_io_header_cpp_test.cpp
  LIBS Salts::NativeIO
  FOLDER "native-io/tests")
set_target_properties(native_io_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)

cmake_add_test(native_ipc_test
  SOURCES native_ipc_test.c
  LIBS Salts::NativeIO Salts::TinyTest Salts::Platform
  FOLDER "native-io/tests")
set_target_properties(native_ipc_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(native_ipc_header_cpp_test
  SOURCES native_ipc_header_cpp_test.cpp
  LIBS Salts::NativeIO
  FOLDER "native-io/tests")
set_target_properties(native_ipc_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
