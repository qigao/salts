add_library(
  capture_common_test_impl STATIC
  ../src/capture_video.c
  fake_capture_backend.c)
cmake_config_target(
  capture_common_test_impl
  FOLDER "capture/tests")
target_compile_features(capture_common_test_impl PUBLIC c_std_11)
target_include_directories(
  capture_common_test_impl
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

cmake_add_test(
  capture_contract_test
  SOURCES test_capture_contract.c
  LIBS capture_common_test_impl TurboUtils::TinyTest
  FOLDER "capture/tests")

if(WIN32)
  cmake_add_test(
    capture_windows_test
    SOURCES test_capture_windows.c
    LIBS TurboUtils::Capture TurboUtils::TinyTest
    FOLDER "capture/tests")
endif()

if(ANDROID)
  cmake_add_test(
    capture_android_test
    SOURCES test_capture_android.c
    LIBS TurboUtils::Capture TurboUtils::TinyTest
    FOLDER "capture/tests")
endif()

if(UNIX AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  cmake_add_test(
    capture_posix_test
    SOURCES test_capture_posix.c
    LIBS TurboUtils::Capture TurboUtils::TinyTest
    FOLDER "capture/tests")
endif()
