cmake_add_test(cflow_fs_test
  SOURCES cflow_fs_test.c cflow_fs_init_failure_test.c
  LIBS TurboUtils::CFlowFS TurboUtils::Platform
       TurboUtils::TinyTest
  FOLDER "cflow-fs/tests")

set_target_properties(cflow_fs_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cflow_fs_header_cpp_test
  SOURCES cflow_fs_header_cpp_test.cpp
  LIBS TurboUtils::CFlowFS TurboUtils::TinyTest
  FOLDER "cflow-fs/tests")

set_target_properties(cflow_fs_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)

cmake_add_test(cflow_fs_watch_test
  SOURCES cflow_fs_watch_test.c
  LIBS TurboUtils::CFlowFS TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "cflow-fs/tests")

set_target_properties(cflow_fs_watch_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

cmake_add_test(cflow_fs_watch_publisher_test
  SOURCES cflow_fs_watch_publisher_test.c
  LIBS TurboUtils::CFlowFS TurboUtils::Platform TurboUtils::TinyTest
  FOLDER "cflow-fs/tests")

set_target_properties(cflow_fs_watch_publisher_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
