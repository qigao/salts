# CFlow macOS and Android Verification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add native macOS Release conformance and Android arm64 Release cross-build/package evidence for the execution foundation.

**Architecture:** Extend the existing CMeta conformance workflow with pinned host jobs. macOS runs owner tests and installed-package consumers; Android uses the repository preset and the runner-provided official NDK toolchain for configure/build/install only.

**Tech Stack:** GitHub Actions, CMake Presets, vcpkg, AppleClang, Android NDK arm64-v8a.

**Spec:** `docs/superpowers/specs/2026-08-23-cflow-execution-model-v2-design.md`

## Global Constraints

- This plan changes workflows/presets only; it does not change CFlow semantics.
- Pin `macos-15` and `ubuntu-24.04`; do not use moving `*-latest` labels.
- macOS must run the same owner-test boundary and package-consumer target as Linux/Windows.
- Android evidence is cross-build/install only and must not be called runtime verification.
- Fail fast when `VCPKG_ROOT`, `ANDROID_NDK_HOME`, or the NDK toolchain file is absent.
- Use `<NDK>/build/cmake/android.toolchain.cmake`, the Android-supported direct-CMake integration.

---

### Task 1: Make macOS Release preset package-complete

**Files:**
- Modify: `presets/ConfigurePresets.json`
- Modify: `presets/TestPresets.json`
- Modify: `presets/BuildPresets.json`

**Interfaces:**
- Produces: visible `release-mac-ninja`, `build-default-mac`, and `test-release-mac` entries with vcpkg package resolution.

- [ ] Add `pkg-vcpkg` to the macOS Release configure inheritance without adding machine-local paths.
- [ ] Add a macOS Release test preset pointing at `build/mac-clang-release` and preserving the runner environment.
- [ ] Run `cmake --list-presets`, `cmake --build --list-presets`, and `ctest --list-presets` on a macOS runner dry run; expected presets are visible only on Darwin.
- [ ] Commit as `build(macos): complete release preset contract`.

### Task 2: Native macOS conformance job

**Files:**
- Modify: `.github/workflows/cmeta.yml`

**Interfaces:**
- Consumes: macOS presets from Task 1.
- Produces: one `macOS 15 release` required check.

- [ ] Add a `macos` job on `macos-15` using `actions/checkout@v6`.
- [ ] Install `re2c` with Homebrew, derive `VCPKG_ROOT` from the runner environment, configure `release-mac-ninja`, and build `build-default-mac`.
- [ ] Run the existing owner regex through `test-release-mac --output-on-failure` and build `verify_installed_package`.
- [ ] Upload CMake/compiler/OS metadata on failure so host-specific issues are reproducible.
- [ ] Dispatch the workflow on the branch and require the macOS job to pass.
- [ ] Commit as `ci(cflow): verify macOS execution foundation`.

### Task 3: Portable Android preset entry

**Files:**
- Modify: `presets/AndroidPresets.json`
- Modify: `presets/BuildPresets.json`
- Modify: `CMakePresets.json`

**Interfaces:**
- Produces: visible `android-arm64-v8a-release` and `build-android-arm64-v8a-release` on Linux CI when required environment variables exist.

- [ ] Split host-local Windows path settings from the portable Android toolchain contract. Keep `VCPKG_CHAINLOAD_TOOLCHAIN_FILE=$env{ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake`, `ANDROID_PLATFORM=android-24`, and `ANDROID_ABI=arm64-v8a` in the portable preset.
- [ ] Add the build preset and retain `BUILD_TESTS=OFF`, `BUILD_EXAMPLES=OFF`, and `BUILD_BENCHMARKS=OFF` for cross-compilation.
- [ ] Validate preset listing with Linux environment variables and confirm zero unresolved Windows-only paths.
- [ ] Commit as `build(android): expose portable arm64 release preset`.

### Task 4: Android cross-build/package job

**Files:**
- Modify: `.github/workflows/cmeta.yml`
- Modify: `docs/superpowers/specs/2026-08-22-cflow-execution-foundation-design.md`
- Modify: `docs/superpowers/specs/2026-08-23-cflow-execution-model-v2-design.md`

**Interfaces:**
- Consumes: portable Android preset from Task 3.
- Produces: one `Android arm64 Release cross-build` check and package artifact.

- [ ] Add an `android` job on pinned `ubuntu-24.04`; validate `ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake` before configure.
- [ ] Configure and build the arm64 Release preset, install to a job-local prefix, and verify exported headers/config/targets exist. Do not attempt to run Android binaries on the host.
- [ ] Upload the install tree, CMake cache, compiler configuration, NDK revision, ABI, and API level as evidence.
- [ ] Dispatch the workflow and require configure/build/install success.
- [ ] Update the host evidence table: macOS becomes native CI verified; Android becomes cross-build/package verified with runtime explicitly absent.
- [ ] Run `git diff --check` and commit as `ci(cflow): verify Android arm64 package build`.
