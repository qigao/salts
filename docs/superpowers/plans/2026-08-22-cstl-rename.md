# Container Rename Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rename the standard-container module from Container to Container across its source layout, public headers, CMake package targets, status API, repository consumers, and documentation without retaining compatibility aliases.

**Architecture:** The container algorithms and concrete `salts_*` APIs remain the single implementation fact source. Only the module identity changes: source directory `cstl/`, internal target `salts_stl`, exported target `Salts::CSTL`, headers under `<cstl/...>`, and module-level status/export identifiers under `salts_stl`/`SALTS_STL`. CMeta container concepts remain named `cmeta_container_*` because they describe a generic protocol rather than this module.

**Tech Stack:** C11, C++17 header probes, CMake 3.20+, CMake Presets, MSVC/Ninja, TinyTest, standalone `find_package(Salts)` consumers.

**Spec:** `docs/superpowers/specs/2026-08-21-container-cmeta-cflow-design.md`

## Global Constraints

- Do not provide `Salts::CSTL`, `<cstl/...>`, `cstl_status`, or `CONTAINER_*` compatibility aliases.
- Preserve all concrete container semantics, algorithms, ownership rules, limits, iterator invalidation, and error distinctions.
- Keep concrete C APIs such as `salts_vec_*`, `salts_map_*`, and `salts_hash_map_*` unchanged.
- Keep generic CMeta concepts such as `cmeta_container_desc` and `CMETA_CONTAINER*` unchanged.
- Use the `win-release-user` configure, build, and test presets under the MSVC developer environment.

---

### Task 1: Lock the new installed-consumer contract

**Files:**
- Modify: `cstl/tests/install_consumer/CMakeLists.txt`
- Modify: `cstl/tests/install_consumer/consumer.c`
- Modify: `cstl/tests/install_consumer/consumer.cpp`

**Interfaces:**
- Consumes: the current build-tree `SaltsConfig.cmake` package.
- Produces: a consumer requiring `Salts::CSTL`, `<cstl/typed.h>`, and `SALTS_STL_OK`.

- [x] **Step 1: Change the C and C++ consumers to the new target, header, and status names**

```cmake
target_link_libraries(cstl_consumer_c PRIVATE Salts::CSTL)
target_link_libraries(cstl_consumer_cpp PRIVATE Salts::CSTL)
```

```c
#include <cstl/typed.h>
if (InstalledInts_init(&values, 2u) != SALTS_STL_OK) return 1;
```

- [x] **Step 2: Configure the consumer against the old build-tree package and verify RED**

Run:

```powershell
cmake -S cstl/tests/install_consumer -B build/container-consumer-red -DCMAKE_PREFIX_PATH=build/Msvc-Release
```

Expected: configure fails because `Salts::CSTL` does not exist yet.

- [x] **Step 3: Commit the failing public-contract test**

```powershell
git add cstl/tests/install_consumer
git commit -m "test(container): require renamed public package contract"
```

### Task 2: Rename the module layout and build targets

**Files:**
- Rename: `cstl/` to `cstl/`
- Rename: `cstl/include/salts/cstl.h` to `cstl/cstl.h`
- Rename: `cstl/include/salts/cstl/` to `cstl/include/cstl/`
- Modify: `CMakeLists.txt`
- Modify: `cstl/CMakeLists.txt`
- Modify: `utils/CMakeLists.txt`
- Modify: `salts_serial/CMakeLists.txt`

**Interfaces:**
- Consumes: unchanged `Salts::CMeta` and concrete `salts_*` source APIs.
- Produces: build target `salts_stl`, alias/export `Salts::CSTL`, library artifact `salts_stl`, and installed `<cstl/...>` headers.

- [x] **Step 1: Move the module and public header tree with Git-aware renames**

```powershell
git mv container container
git mv cstl/include/salts/container cstl/include/container
git mv cstl/include/salts/cstl.h cstl/cstl.h
```

- [x] **Step 2: Update the root build graph and module target identity**

Use `add_subdirectory(cstl)`, `set(TARGET_NAME salts_stl)`, alias `Salts::CSTL`, export name `STL`, and IDE folder `container`.

- [x] **Step 3: Update repository target consumers and test/benchmark targets**

Replace links to `Salts::CSTL` with `Salts::CSTL`; rename module-local test and benchmark targets from `cstl_*` to `cstl_*` while preserving their test behavior.

- [x] **Step 4: Fresh-configure and build the new library target**

Run:

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target salts_stl
```

Expected: configure and target build succeed with a `salts_stl` artifact.

- [x] **Step 5: Commit the module/build rename**

```powershell
git add CMakeLists.txt container utils salts_serial
git commit -m "refactor(container): rename container module and targets"
```

### Task 3: Rename the public status/export surface and callers

**Files:**
- Modify: `cstl/include/cstl/*.h`
- Modify: `cstl/src/*.c`
- Rename: `utils/src/salts_cstl_status_internal.h` to `utils/src/salts_stl_status_internal.h`
- Modify: `utils/src/ac_automaton.c`
- Modify: `utils/src/levenshtein_automaton.c`
- Modify: `salts_serial/*.c`
- Modify: `salts_serial/*.h`
- Modify: `salts_serial/test/*.c`
- Modify: `cstl/tests/*`

**Interfaces:**
- Consumes: the existing status meanings and concrete algorithm behavior.
- Produces: `salts_stl_status`, `SALTS_STL_OK`, `SALTS_STL_INVALID_ARGUMENT`, `SALTS_STL_OUT_OF_MEMORY`, `SALTS_STL_CAPACITY_EXCEEDED`, `SALTS_STL_EMPTY`, `SALTS_STL_NOT_FOUND`, `SALTS_STL_TYPE_MISMATCH`, and `SALTS_STL_TRAIT_MISSING`.

- [x] **Step 1: Rename module guards, status type/constants, and internal typed-schema macros**

Apply exact mechanical substitutions only inside the Container module and its direct consumers; do not rename generic CMeta container protocols.

- [x] **Step 2: Rename Core and TurboSerial status adapters**

Rename module-specific adapter identifiers and includes from `container` to `stl`, preserving their existing status mappings.

- [x] **Step 3: Build and run the focused module and adjacent tests**

Run:

```powershell
cmake --build --preset win-release-user --target cstl_header_test cstl_header_cpp_test cstl_typed_test test_string_automata salts_serial
ctest --preset win-release-user -R "^(cstl_|test_string_automata)"
```

Expected: all selected targets build and selected tests pass.

- [x] **Step 4: Commit the public API/caller rename**

```powershell
git add container utils salts_serial
git commit -m "refactor(container): rename public status and header surface"
```

### Task 4: Synchronize documentation and verify the installed package

**Files:**
- Modify: `AGENTS.md`
- Modify: `cstl/README.md`
- Modify: `cmeta/README.md`
- Modify: `cmeta/C_META_CAPABILITIES.md`
- Modify: `docs/superpowers/specs/2026-08-21-container-cmeta-cflow-design.md`
- Modify: relevant `docs/superpowers/plans/*.md`

**Interfaces:**
- Consumes: the implemented target/header/status mapping.
- Produces: repository documentation and install examples that name only Container.

- [x] **Step 1: Replace obsolete module names in active documentation and examples**

Document `Container`, `Salts::CSTL`, `cstl/`, `<cstl/...>`, and `SALTS_STL_*`; retain the lowercase word “container” where it describes the data-structure concept.

- [x] **Step 2: Scan for forbidden old public names**

Run:

```powershell
rg.exe -n "Salts::CSTL|salts_cstl|<cstl|cstl_status|CONTAINER_[A-Z_]" . --glob "!build/**" --glob "!vcpkg_installed/**" --glob "!.codegraph/**" --glob "!vendor/**"
```

Expected: no matches outside historical prose that explicitly explains the migration; preferred final state is zero matches.

- [x] **Step 3: Run fresh configure, full build, and full CTest**

Run:

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
```

Expected: configure/build succeed and all registered tests pass.

- [x] **Step 4: Install to an isolated prefix and build both standalone consumers**

Run:

```powershell
cmake --install build/Msvc-Release --prefix build/container-install-smoke
cmake -S cstl/tests/install_consumer -B build/container-consumer -DCMAKE_PREFIX_PATH=build/container-install-smoke
cmake --build build/container-consumer
```

Expected: both C11 and C++17 consumers configure and link only through `Salts::CSTL` and installed `<cstl/...>` headers.

- [x] **Step 5: Check formatting, index state, and commit documentation**

```powershell
git diff --check
codegraph sync .
git add AGENTS.md cmeta docs container
git commit -m "docs(container): publish renamed standard library module"
```
