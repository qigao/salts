add_library(
  turbo_capture SHARED
  src/capture_video.c)

cmake_config_target(
  turbo_capture
  ALIAS TurboUtils::Capture
  FOLDER "capture"
  EXPORT_NAME Capture)
target_compile_features(turbo_capture PUBLIC c_std_11)
target_compile_definitions(
  turbo_capture
  PRIVATE TURBO_CAPTURE_SHARED TURBO_CAPTURE_BUILD
  INTERFACE TURBO_CAPTURE_SHARED)

target_include_directories(
  turbo_capture
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(turbo_capture PRIVATE TurboUtils::Core)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  find_path(TURBO_CAPTURE_MINIAUDIO_INCLUDE_DIR NAMES miniaudio.h REQUIRED)
  target_sources(turbo_capture PRIVATE src/miniaudio_impl.c)
  target_include_directories(
    turbo_capture PRIVATE ${TURBO_CAPTURE_MINIAUDIO_INCLUDE_DIR})
endif()

if(WIN32 OR ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(libyuv CONFIG REQUIRED)
  target_link_libraries(turbo_capture PRIVATE yuv)
endif()

if(WIN32)
  find_package(libjpeg-turbo CONFIG REQUIRED)
  install(FILES "$<TARGET_FILE:yuv>" "$<TARGET_FILE:libjpeg-turbo::jpeg>"
          DESTINATION bin)
endif()

if(ANDROID)
  target_sources(
    turbo_capture
    PRIVATE src/android/capture_android.c
            src/android/capture_audio_android.c
            src/android/capture_video_android.c
            src/android/capture_screen_android.c)
  find_library(TURBO_CAPTURE_ANDROID_LIBRARY android REQUIRED)
  find_library(TURBO_CAPTURE_ANDROID_OPENSL_LIBRARY OpenSLES REQUIRED)
  find_library(TURBO_CAPTURE_ANDROID_MEDIA_LIBRARY mediandk REQUIRED)
  find_library(TURBO_CAPTURE_ANDROID_CAMERA_LIBRARY camera2ndk REQUIRED)
  target_compile_definitions(turbo_capture PRIVATE TURBO_CAPTURE_ANDROID=1)
  target_link_libraries(
    turbo_capture
    PRIVATE ${TURBO_CAPTURE_ANDROID_LIBRARY}
            ${TURBO_CAPTURE_ANDROID_OPENSL_LIBRARY}
            ${TURBO_CAPTURE_ANDROID_MEDIA_LIBRARY}
            ${TURBO_CAPTURE_ANDROID_CAMERA_LIBRARY}
            ${CMAKE_DL_LIBS})
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  enable_language(OBJC)
  target_sources(
    turbo_capture
    PRIVATE src/ios/capture_ios.m src/ios/capture_ios_guard.m
            src/ios/capture_audio_ios.m
            src/ios/capture_video_ios.m src/ios/capture_screen_ios.m)
  target_include_directories(turbo_capture PRIVATE src/ios)
  target_compile_definitions(turbo_capture PRIVATE TURBO_CAPTURE_IOS=1)
  target_compile_options(turbo_capture PRIVATE -fobjc-arc)
  find_library(TURBO_CAPTURE_FOUNDATION_FRAMEWORK Foundation REQUIRED)
  find_library(TURBO_CAPTURE_AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
  find_library(TURBO_CAPTURE_COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
  find_library(TURBO_CAPTURE_COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
  find_library(TURBO_CAPTURE_AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
  find_library(TURBO_CAPTURE_COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
  find_library(TURBO_CAPTURE_REPLAYKIT_FRAMEWORK ReplayKit REQUIRED)
  find_library(TURBO_CAPTURE_UIKIT_FRAMEWORK UIKit REQUIRED)
  target_link_libraries(
    turbo_capture
    PRIVATE ${TURBO_CAPTURE_FOUNDATION_FRAMEWORK}
            ${TURBO_CAPTURE_AVFOUNDATION_FRAMEWORK}
            ${TURBO_CAPTURE_COREMEDIA_FRAMEWORK}
            ${TURBO_CAPTURE_COREVIDEO_FRAMEWORK}
            ${TURBO_CAPTURE_AUDIOTOOLBOX_FRAMEWORK}
            ${TURBO_CAPTURE_COREAUDIO_FRAMEWORK}
            ${TURBO_CAPTURE_REPLAYKIT_FRAMEWORK}
            ${TURBO_CAPTURE_UIKIT_FRAMEWORK})
  set_target_properties(
    turbo_capture
    PROPERTIES XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "14.0"
               XCODE_ATTRIBUTE_ENABLE_BITCODE "NO")
elseif(WIN32)
  target_sources(
    turbo_capture
    PRIVATE src/capture_audio_miniaudio.c src/capture_win32.c
            src/capture_video_win32.c
            src/capture_screen_win32.c)
  target_link_libraries(
    turbo_capture
    PRIVATE mfplat mf mfuuid mfreadwrite strmiids ole32 oleaut32 d3d11 dxgi
            dxguid)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(
    turbo_capture
    PRIVATE src/capture_audio_miniaudio.c src/capture_linux.c
            src/capture_video_linux.c
            src/capture_screen_linux.c)
  target_compile_definitions(
    turbo_capture PRIVATE TURBO_HAS_PIPEWIRE TURBO_HAS_X11)
  pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
  find_package(X11 REQUIRED COMPONENTS Xext)
  target_link_libraries(
    turbo_capture PRIVATE Threads::Threads PkgConfig::PIPEWIRE X11::X11
                          X11::Xext ${CMAKE_DL_LIBS} m)
elseif(APPLE)
  enable_language(OBJC)
  target_sources(
    turbo_capture
    PRIVATE src/capture_audio_miniaudio.c src/capture_macos.c
            src/capture_video_macos.m
            src/capture_screen_macos.m)
  find_library(TURBO_CAPTURE_AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
  find_library(TURBO_CAPTURE_COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
  find_library(TURBO_CAPTURE_AUDIO_TOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
  find_library(TURBO_CAPTURE_COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
  find_library(TURBO_CAPTURE_COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
  find_library(TURBO_CAPTURE_COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
  find_library(TURBO_CAPTURE_IOSURFACE_FRAMEWORK IOSurface REQUIRED)
  target_link_libraries(
    turbo_capture
    PRIVATE ${TURBO_CAPTURE_AVFOUNDATION_FRAMEWORK}
            ${TURBO_CAPTURE_COREAUDIO_FRAMEWORK}
            ${TURBO_CAPTURE_AUDIO_TOOLBOX_FRAMEWORK}
            ${TURBO_CAPTURE_COREFOUNDATION_FRAMEWORK}
            ${TURBO_CAPTURE_COREGRAPHICS_FRAMEWORK}
            ${TURBO_CAPTURE_COREVIDEO_FRAMEWORK}
            ${TURBO_CAPTURE_IOSURFACE_FRAMEWORK})
endif()

install(
  TARGETS turbo_capture
  EXPORT TurboUtilsTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin)
install(
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
  DESTINATION include
  FILES_MATCHING
  PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
