# TurboSTL Rename Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rename the standard-container module from Container to TurboSTL across its source layout, public headers, CMake package targets, status API, repository consumers, and documentation without retaining compatibility aliases.

**Architecture:** The container algorithms and concrete `turbo_*` APIs remain the single implementation fact source. Only the module identity changes: source directory `turbostl/`, internal target `turbo_stl`, exported target `TurboUtils::STL`, headers under `<turbo/stl/...>`, and module-level status/export identifiers under `turbo_stl`/`TURBO_STL`. CMeta container concepts remain named `cmeta_container_*` because they describe a generic protocol rather than this module.

**Tech Stack:** C11, C++17 header probes, CMake 3.20+, CMake Presets, MSVC/Ninja, TinyTest, standalone `find_package(TurboUtils)` consumers.

**Spec:** `docs/superpowers/specs/2026-08-21-container-cmeta-cflow-design.md`

## Global Constraints

- Do not provide `TurboUtils::Container`, `<turbo/container/...>`, `container_status`, or `CONTAINER_*` compatibility aliases.
- Preserve all concrete container semantics, algorithms, ownership rules, limits, iterator invalidation, and error distinctions.
- Keep concrete C APIs such as `turbo_vec_*`, `turbo_map_*`, and `turbo_hash_map_*` unchanged.
- Keep generic CMeta concepts such as `cmeta_container_desc` and `CMETA_CONTAINER*` unchanged.
- Use the `win-release-user` configure, build, and test presets under the MSVC developer environment.

---

### Task 1: Lock the new installed-consumer contract

**Files:**
- Modify: `container/tests/install_consumer/CMakeLists.txt`
- Modify: `container/tests/install_consumer/consumer.c`
- Modify: `container/tests/install_consumer/consumer.cpp`

**Interfaces:**
- Consumes: the current build-tree `TurboUtilsConfig.cmake` package.
- Produces: a consumer requiring `TurboUtils::STL`, `<turbo/stl/typed.h>`, and `TURBO_STL_OK`.

- [ ] **Step 1: Change the C and C++ consumers to the new target, header, and status names**

```cmake
target_link_libraries(turbostl_consumer_c PRIVATE TurboUtils::STL)
target_link_libraries(turbostl_consumer_cpp PRIVATE TurboUtils::STL)
```

```c
#include <turbo/stl/typed.h>
if (InstalledInts_init(&values, 2u) != TURBO_STL_OK) return 1;
```

- [ ] **Step 2: Configure the consumer against the old build-tree package and verify RED**

Run:

```powershell
cmake -S container/tests/install_consumer -B build/turbostl-consumer-red -DCMAKE_PREFIX_PATH=build/Msvc-Release
```

Expected: configure fails because `TurboUtils::STL` does not exist yet.

- [ ] **Step 3: Commit the failing public-contract test**

```powershell
git add container/tests/install_consumer
git commit -m "test(turbostl): require renamed public package contract"
```

### Task 2: Rename the module layout and build targets

**Files:**
- Rename: `container/` to `turbostl/`
- Rename: `turbostl/include/turbo/container.h` to `turbostl/include/turbo/stl.h`
- Rename: `turbostl/include/turbo/container/` to `turbostl/include/turbo/stl/`
- Modify: `CMakeLists.txt`
- Modify: `turbostl/CMakeLists.txt`
- Modify: `utils/CMakeLists.txt`
- Modify: `turbo_serial/CMakeLists.txt`

**Interfaces:**
- Consumes: unchanged `TurboUtils::CMeta` and concrete `turbo_*` source APIs.
- Produces: build target `turbo_stl`, alias/export `TurboUtils::STL`, library artifact `turbo_stl`, and installed `<turbo/stl/...>` headers.

- [ ] **Step 1: Move the module and public header tree with Git-aware renames**

```powershell
git mv container turbostl
git mv turbostl/include/turbo/container turbostl/include/turbo/stl
git mv turbostl/include/turbo/container.h turbostl/include/turbo/stl.h
```

- [ ] **Step 2: Update the root build graph and module target identity**

Use `add_subdirectory(turbostl)`, `set(TARGET_NAME turbo_stl)`, alias `TurboUtils::STL`, export name `STL`, and IDE folder `turbostl`.

- [ ] **Step 3: Update repository target consumers and test/benchmark targets**

Replace links to `TurboUtils::Container` with `TurboUtils::STL`; rename module-local test and benchmark targets from `container_*` to `turbostl_*` while preserving their test behavior.

- [ ] **Step 4: Fresh-configure and build the new library target**

Run:

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target turbo_stl
```

Expected: configure and target build succeed with a `turbo_stl` artifact.

- [ ] **Step 5: Commit the module/build rename**

```powershell
git add CMakeLists.txt turbostl utils turbo_serial
git commit -m "refactor(turbostl): rename container module and targets"
```

### Task 3: Rename the public status/export surface and callers

**Files:**
- Modify: `turbostl/include/turbo/stl/*.h`
- Modify: `turbostl/src/*.c`
- Rename: `utils/src/turbo_container_status_internal.h` to `utils/src/turbo_stl_status_internal.h`
- Modify: `utils/src/ac_automaton.c`
- Modify: `utils/src/levenshtein_automaton.c`
- Modify: `turbo_serial/*.c`
- Modify: `turbo_serial/*.h`
- Modify: `turbo_serial/test/*.c`
- Modify: `turbostl/tests/*`

**Interfaces:**
- Consumes: the existing status meanings and concrete algorithm behavior.
- Produces: `turbo_stl_status`, `TURBO_STL_OK`, `TURBO_STL_INVALID_ARGUMENT`, `TURBO_STL_OUT_OF_MEMORY`, `TURBO_STL_CAPACITY_EXCEEDED`, `TURBO_STL_EMPTY`, `TURBO_STL_NOT_FOUND`, `TURBO_STL_TYPE_MISMATCH`, `TURBO_STL_TRAIT_MISSING`, and `TURBO_STL_API`.

- [ ] **Step 1: Rename module guards, export macros, status type/constants, and internal typed-schema macros**

Apply exact mechanical substitutions only inside the TurboSTL module and its direct consumers; do not rename generic CMeta container protocols.

- [ ] **Step 2: Rename Core and TurboSerial status adapters**

Rename module-specific adapter identifiers and includes from `container` to `stl`, preserving their existing status mappings.

- [ ] **Step 3: Build and run the focused module and adjacent tests**

Run:

```powershell
cmake --build --preset win-release-user --target turbostl_header_test turbostl_header_cpp_test turbostl_typed_test test_string_automata turbo_serial
ctest --preset win-release-user -R "^(turbostl_|test_string_automata)"
```

Expected: all selected targets build and selected tests pass.

- [ ] **Step 4: Commit the public API/caller rename**

```powershell
git add turbostl utils turbo_serial
git commit -m "refactor(turbostl): rename public status and header surface"
```

### Task 4: Synchronize documentation and verify the installed package

**Files:**
- Modify: `AGENTS.md`
- Modify: `turbostl/README.md`
- Modify: `cmeta/README.md`
- Modify: `cmeta/C_META_CAPABILITIES.md`
- Modify: `docs/superpowers/specs/2026-08-21-container-cmeta-cflow-design.md`
- Modify: relevant `docs/superpowers/plans/*.md`

**Interfaces:**
- Consumes: the implemented target/header/status mapping.
- Produces: repository documentation and install examples that name only TurboSTL.

- [ ] **Step 1: Replace obsolete module names in active documentation and examples**

Document `TurboSTL`, `TurboUtils::STL`, `turbostl/`, `<turbo/stl/...>`, and `TURBO_STL_*`; retain the lowercase word “container” where it describes the data-structure concept.

- [ ] **Step 2: Scan for forbidden old public names**

Run:

```powershell
rg.exe -n "TurboUtils::Container|turbo_container|<turbo/container|container_status|CONTAINER_[A-Z_]" . --glob "!build/**" --glob "!vcpkg_installed/**" --glob "!.codegraph/**" --glob "!vendor/**"
```

Expected: no matches outside historical prose that explicitly explains the migration; preferred final state is zero matches.

- [ ] **Step 3: Run fresh configure, full build, and full CTest**

Run:

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
```

Expected: configure/build succeed and all registered tests pass.

- [ ] **Step 4: Install to an isolated prefix and build both standalone consumers**

Run:

```powershell
cmake --install build/Msvc-Release --prefix build/turbostl-install-smoke
cmake -S turbostl/tests/install_consumer -B build/turbostl-consumer -DCMAKE_PREFIX_PATH=build/turbostl-install-smoke
cmake --build build/turbostl-consumer
```

Expected: both C11 and C++17 consumers configure and link only through `TurboUtils::STL` and installed `<turbo/stl/...>` headers.

- [ ] **Step 5: Check formatting, index state, and commit documentation**

```powershell
git diff --check
codegraph sync .
git add AGENTS.md cmeta docs turbostl
git commit -m "docs(turbostl): publish renamed standard library module"
```
