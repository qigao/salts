# Windows CI Cache and Step Split Specification

## Goal

Reduce repeated Windows CI time and make failures attributable to configure,
build, test, install, or package-consumer verification.

## Evidence

- The 2026-09-02 `Windows release` run spent about 12 minutes 24 seconds in
  configure while vcpkg built dependencies, about 4 minutes 21 seconds on the
  921-target project build, and about 11 seconds on the selected tests.
- The concurrent IOCP job spent about 10 minutes in configure before its
  71-target project build started.
- Both jobs use the same manifest, baseline, `x64-windows` triplet, MSVC toolchain,
  and `win-release-user` preset, so they can use the same content-addressed vcpkg
  binary archive cache.

## Requirements

- Cache vcpkg binary archives, not `build/Msvc-Release` or the installed package
  tree.
- Use a cache key containing runner OS, runner architecture, runner image
  identity, target triplet, and `vcpkg.json` hash.
- Permit prefix restoration across manifest or runner-image changes; vcpkg's ABI
  validation remains authoritative for whether an archive can be reused.
- Configure `VCPKG_BINARY_SOURCES` with an absolute, job-local filesystem path in
  read/write mode and fail fast when vcpkg or Visual Studio discovery fails.
- Preserve the existing CMake and CTest presets, target lists, test filters, and
  user-visible pass/fail semantics.
- Split CMeta Windows work into environment setup, cache restore, configure,
  build, test, install, and installed-package verification steps.
- Split NativeIO Windows configure and build into distinct steps without changing
  Linux or macOS behavior.
- Each command step must initialize MSVC through `VsDevCmd.bat`; environment from
  one GitHub Actions process must not be assumed to persist to another.

## Verification

- Parse both workflow files as YAML and lint them with `actionlint` when
  available.
- Assert that both Windows jobs use the same cache key schema and binary source
  directory.
- List CMake configure/build/test presets and run the existing Windows release
  configure plus the smallest workflow-relevant build and tests locally.
- Open a pull request and compare the first cold-cache run with a subsequent
  cache-hit run; a cold first run is expected and is not a regression.
