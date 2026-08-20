project(tinytest)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(tinytest INTERFACE)
cmake_config_target(tinytest
  ALIAS TurboUtils::TinyTest
  FOLDER "tinytest"
  EXPORT_NAME TinyTest)

target_include_directories(
  tinytest INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                     $<INSTALL_INTERFACE:include>)

install(TARGETS tinytest EXPORT TurboUtilsTargets)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/tinytest.h
               ${CMAKE_CURRENT_SOURCE_DIR}/tinytest_impl.h
               ${CMAKE_CURRENT_SOURCE_DIR}/tinytest_meta.h
               ${CMAKE_CURRENT_SOURCE_DIR}/tinytest.hpp
               ${CMAKE_CURRENT_SOURCE_DIR}/tinymock.h
               ${CMAKE_CURRENT_SOURCE_DIR}/tinymock_impl.h
        DESTINATION include)

# Examples live in examples/ and are plain executables rather than CTest cases.
add_subdirectory(examples)

# Tests live in test/ and register executable targets and CTest entries.
add_subdirectory(test)
