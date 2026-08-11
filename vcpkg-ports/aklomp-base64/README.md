# aklomp-base64 overlay port

This overlay replaces the official `aklomp-base64` port selected by the vcpkg
baseline `b1b19307e2d2ec1eefbdb7ea069de7d4bcd31f01` and keeps version 0.5.2.

Local changes are intentionally narrow:

- force static library linkage (`vcpkg_check_linkage(ONLY_STATIC_LIBRARY)` plus
  `-DBUILD_SHARED_LIBS=OFF`);
- pin the upstream source to commit
  `9e8ed65048ff0f703fad3deb03bf66ac7f78a4d7` instead of the `v0.5.2` tag.

The pinned commit is the post-0.5.2 master snapshot that this repository
previously shipped from `vendor/base64` (Windows UTF-8 CLI fixes and
NEON-on-Windows-ARM64 support). Building it keeps the vcpkg package
behavior-identical to that previous static build. The official port builds the
older `v0.5.2` tag, which lacks those changes.

Consumed through the upstream CMake package:

```cmake
find_package(base64 CONFIG REQUIRED)
target_link_libraries(main PRIVATE aklomp::base64)
```