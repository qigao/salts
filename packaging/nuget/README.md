# Salts.Native

Prebuilt Release install trees for qigao/salts, published as a lightweight NuGet package.

The package contains native SDK payloads only. Source files, tests, examples, benchmarks,
and CI caches are intentionally excluded.

## Package layout

- `sdk/linux-x64/` - Linux x64 install prefix
- `sdk/windows-x64/` - Windows x64 install prefix
- `sdk/macos-x64/` or `sdk/macos-arm64/` - macOS install prefix matching the release runner
- `sdk/android-arm64-v8a/` - Android arm64-v8a install prefix

Each target directory contains the normal Salts CMake install tree, including headers,
libraries, and `lib/cmake/Salts/SaltsConfig.cmake`.

Point `CMAKE_PREFIX_PATH` at the matching SDK directory and use:

    find_package(Salts CONFIG REQUIRED)

Android is a target SDK in this package. Host build tools such as re2c are distributed
separately and run on the build host rather than on the Android target.
