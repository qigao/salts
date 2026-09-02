# Rocida CMake Package Rename Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `Rocida` the sole project/package identity and move the TurboSTL public include surface to `rocida/stl` while preserving C symbols and physical `turbo_*` library names.

**Architecture:** Perform a deliberate breaking rename with one package owner and no compatibility aliases or forwarding headers. Keep the installed consumer as the contract test, migrate package metadata and the public STL include tree to Rocida-owned paths, then validate source-tree and isolated installed consumption.

**Tech Stack:** CMake 3.20+, CMake Presets, CTest, PowerShell, MSVC/Ninja, vcpkg.

**Spec:** Current-session user directives: “先改本库，包括install”, “不要alias”, and “不应该再出现任何TurboUtil turobstl的头文件目录应该考虑改变”.

## Global Constraints

- The canonical package is `Rocida`; do not add `TurboUtils` compatibility packages or cross-package aliases.
- Preserve all `turbo_*` C APIs, source directory names, and physical library output names.
- Install STL headers only under `include/rocida/stl` with aggregate header `include/rocida/stl.h`; do not install forwarding headers under the former public include path.
- Current first-party code and documentation must use the Rocida brand; historical plans/specs and required skill identifiers are not runtime/public compatibility surfaces.
- Rename `TURBOUTILS_ROOT` to `ROCIDA_ROOT` and install under `$env{PKG_ROOT}/rocida/<profile>` or `$env{PKG_ROOT}/rocida-android/<profile>`.
- Do not modify the unrelated untracked `tools/wsparser/` directory.
- Windows configure, build, test, and install verification must use checked-in user presets from a Visual Studio developer environment.

---

### Task 1: Establish the failing installed-package contract

**Files:**
- Modify: `tests/install_consumer/CMakeLists.txt`
- Modify: `cmake/VerifyInstalledPackage.cmake`

**Interfaces:**
- Consumes: the package installed by the current build tree.
- Produces: a consumer requiring `find_package(Rocida CONFIG REQUIRED)` and linking real `Rocida::*` imported targets.

- [x] **Step 1: Rename only the installed-consumer expectation to Rocida**

  Change the consumer project, helper name, package lookup, target names, feature checks, package directory, and diagnostic messages from `TurboUtils` to `Rocida`.

- [x] **Step 2: Run the existing installed-package target and verify RED**

  Run `cmake --build --preset win-release-user --target verify_installed_package` from `VsDevCmd.bat`.

  Expected: failure while configuring the consumer because the installed producer still emits `TurboUtilsConfig.cmake`, proving the test catches the missing `Rocida` package.

### Task 2: Rename the package producer and complete in-tree CMake graph

**Files:**
- Rename: `cmake/TurboUtilsConfig.cmake.in` to `cmake/RocidaConfig.cmake.in`
- Modify: root and module `CMakeLists.txt` files
- Modify: `cmake/*.cmake`
- Modify: `CMakeUserPresets.json`

**Interfaces:**
- Consumes: existing concrete targets such as `turbo_cmeta` and their current `EXPORT_NAME` values.
- Produces: `RocidaConfig.cmake`, `RocidaTargets.cmake`, `Rocida::*`, `ROCIDA_ROOT`, and Rocida install prefixes.

- [x] **Step 1: Rename the top-level project and package artifacts**

  Set `project(Rocida)`, generate/install `RocidaConfig.cmake` and `RocidaConfigVersion.cmake`, export `RocidaTargets`, and use `NAMESPACE Rocida::` under `lib/cmake/Rocida`.

- [x] **Step 2: Rename every in-tree public alias and export-set reference**

  Replace `TurboUtils::*` with `Rocida::*` and `TurboUtilsTargets` with `RocidaTargets` throughout first-party CMake files, preserving concrete target and output names.

- [x] **Step 3: Rename preset root and install paths**

  Replace `TURBOUTILS_ROOT` with `ROCIDA_ROOT`, and derive host/Android install prefixes from `rocida` and `rocida-android` respectively.

- [x] **Step 4: Reconfigure and run the smallest package verification target**

  Run `cmake --fresh --preset win-release-user`, then `cmake --build --preset win-release-user --target verify_installed_package`.

  Expected: the real installed consumer configures and builds using only `Rocida::*` targets.

### Task 3: Synchronize current consumers and documentation

**Files:**
- Modify: first-party tests, examples, and current README/book/architecture files that show CMake package or target usage
- Exclude: historical implementation plans and vendored upstream documentation

**Interfaces:**
- Consumes: `find_package(Rocida)` and `Rocida::*` from Task 2.
- Produces: current examples and documentation that match the install contract.

- [x] **Step 1: Locate remaining public CMake identifiers**

  Search first-party current files for `TurboUtils::`, `find_package(TurboUtils`, `TurboUtilsConfig`, `TurboUtilsTargets`, and exact `TURBOUTILS_ROOT`.

- [x] **Step 2: Update active consumers and documentation**

  Replace only public CMake/package references; retain historical prose and C identifiers where they describe the old source/API prefix intentionally.

- [x] **Step 3: Verify no active old package surface remains**

  Repeat the search excluding historical plans, vendored sources, build trees, and installed dependency trees. Expected: no active public CMake/package matches.

### Task 4: Full verification

**Files:**
- Verify only; no new production files.

**Interfaces:**
- Consumes: the completed Rocida producer and consumers.
- Produces: reproducible evidence for configure, build, tests, install, and installed consumption.

- [x] **Step 1: Validate preset discovery**

  Run `cmake --list-presets`, `cmake --build --list-presets`, and `ctest --list-presets`.

- [x] **Step 2: Configure and build Release**

  Run `cmake --fresh --preset win-release-user` and `cmake --build --preset win-release-user` from `VsDevCmd.bat`.

- [x] **Step 3: Run tests and installed-package smoke test**

  Run `ctest --preset win-release-user` and `cmake --build --preset win-release-user --target verify_installed_package`.

- [x] **Step 4: Verify the checked-in install preset**

  Run `cmake --build --preset install-win-release-user` and confirm the install contains `lib/cmake/Rocida/RocidaConfig.cmake` plus `RocidaTargets.cmake`.

- [x] **Step 5: Review the diff and worktree**

  Confirm `tools/wsparser/` remains untouched, no generated build/install artifacts are staged as source changes, and all edits stay within the authorized CMake/package rename.

### Task 5: Move the public STL include surface

**Files:**
- Move: `turbostl/include/turbostl/*.h` to `turbostl/include/rocida/stl/*.h`
- Move: `turbostl/include/turbostl.h` to `turbostl/include/rocida/stl.h`
- Modify: `turbostl/CMakeLists.txt`
- Modify: first-party sources, tests, examples, and current documentation that include the old path
- Test: `tests/install_consumer/consumer.c`
- Test: `cmake/VerifyInstalledPackage.cmake`

**Interfaces:**
- Consumes: the existing `Rocida::STL` and `Rocida::STLStream` targets.
- Produces: `<rocida/stl/*.h>` and `<rocida/stl.h>` as the sole installed STL include surface.

- [x] **Step 1: Change the installed consumer to the Rocida include path**

  Replace the STL consumer includes with `<rocida/stl/typed.h>` and `<rocida/stl/stream.h>`. Require the installed package to contain those files and reject an installed `include/turbostl` directory.

- [x] **Step 2: Run the installed-package target and verify RED**

  Run `cmake --build --preset win-release-user --target verify_installed_package` from `VsDevCmd.bat`.

  Expected: consumer compilation or install-layout validation fails because the producer still installs the former include tree.

- [x] **Step 3: Move headers and rewrite first-party includes**

  Move the physical public headers to `turbostl/include/rocida/stl`, move the aggregate to `turbostl/include/rocida/stl.h`, update internal include directives, and install only the `include/rocida` tree.

- [x] **Step 4: Reconfigure and verify the isolated consumer GREEN**

  Run `cmake --fresh --preset win-release-user`, then build `verify_installed_package`. Expected: the isolated 44-step consumer build succeeds and its install tree contains only the Rocida STL header path.

### Task 6: Add the root entry document and remove active legacy branding

**Files:**
- Create: `README.md`
- Modify: `turbostl/README.md`
- Modify: current first-party README, architecture, manifest, comments, and diagnostic identifiers containing the former package brand
- Exclude: historical `docs/superpowers/plans`, `docs/superpowers/specs`, and required global skill identifiers in `AGENTS.md`

**Interfaces:**
- Consumes: the final package, target, and include contracts from Tasks 2 and 5.
- Produces: a Rocida-first repository entry page and current documentation without the former package brand.

- [x] **Step 1: Document Rocida's modern C model**

  Describe CMeta, CFlow, TurboSTL, data binding, execution/network modules, build/test/install commands, installed CMake consumption, and the `Rocida::STL` to `<rocida/stl/...>` mapping.

- [x] **Step 2: Clean current first-party branding**

  Replace the former package name in active README/architecture text, manifest metadata, comments, diagnostics, and platform identifiers without renaming `turbo_*` C APIs or physical libraries.

- [x] **Step 3: Run final static and build verification**

  Verify current files contain no former package identity or former positive STL includes; run Release build, all CTest tests, isolated installed consumer, and install preset.

- [ ] **Step 4: Amend the PR and wait for new CI**

  Commit the header migration and root README, push the branch, update PR #205, then merge only after every required GitHub check passes.
