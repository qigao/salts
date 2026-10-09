set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME iOS)

# ICU 74's Autoconf host matching predates vcpkg's apple-ios host name.
# iOS uses the Darwin make fragment; keep vcpkg's SDK and compiler flags.
if(PORT STREQUAL "icu")
  # arm64 iOS and arm64 macOS still require cross compilation. Distinct host
  # names prevent Autoconf from attempting to execute an iPhoneOS binary.
  set(VCPKG_MAKE_BUILD_TRIPLET --host=aarch64-apple-ios)
  set(VCPKG_MAKE_CONFIGURE_OPTIONS icu_cv_host_frag=mh-darwin)
endif()
