# Container Module Extraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Rename and promote the experimental Turbo container implementation into a first-class `container` module with `container_*` raw C APIs and `TurboUtils::Container` CMake integration.

**Architecture:** Raw data structures live in `container/` with a stable `container_*` namespace. The CMeta bridge lives in `container/meta.h` and `container/typed.h`; CFlow continues to consume CMeta descriptors rather than raw container APIs. Project-wide `TURBO_*` status/error constants remain until a later core extraction.

**Tech Stack:** ISO C11, CMake, GCC/Clang, CMeta, CFlow.

**Spec:** `docs/superpowers/specs/2026-08-20-container-module-extraction-design.md`

## Global Constraints

- No compatibility aliases for old container `turbo_*` symbols or headers.
- Do not rename project-wide `TURBO_OK`, `TURBO_EINVAL`, `TURBO_ENOMEM` in this change.
- Preserve strict ISO C11 compilation with `-std=c11 -pedantic-errors -Wall -Wextra -Werror`.
- Preserve CMeta -> no CFlow and container -> no CFlow dependency boundaries.
- Keep algorithms behaviorally unchanged; this is a module/naming refactor.

---

### Task 1: Add rename boundary test

**Files:**
- Modify: `Makefile`

**Interfaces:**
- Consumes: existing `boundary` verification target.
- Produces: a failing check until the old module/name surface is removed.

- [x] Add checks that reject `-Iturbo/include`, `turbo/src/`, `<turbo_*.h>`, and container raw identifiers beginning with `turbo_(vec|deque|list|stack|queue|heap|set|hash_set|hash_map|map|multimap|btree|bplus_tree)_` in migrated tests/docs/build files.
- [x] Run `make boundary`; verify it fails on the current tree.

### Task 2: Rename module files and raw C namespace

**Files:**
- Rename: `turbo/` -> `container/`
- Rename headers to `container/include/container/*.h`
- Rename sources to `container/src/*.c`
- Modify all renamed headers/sources.

**Interfaces:**
- Produces raw types/functions such as `container_vec_t`, `container_vec_init`, `container_list_t`, `container_hash_map_put`.

- [x] Rename files/directories.
- [x] Rename container-specific types/functions/macros from `turbo_*`/`TURBO_*_DEFINE`/`TURBO_META_*` to `container_*`/`CONTAINER_*_DEFINE`/`CONTAINER_META_*`.
- [x] Keep `TURBO_OK`/`TURBO_EINVAL`/`TURBO_ENOMEM` unchanged.
- [x] Compile raw container smoke/property tests and fix only rename fallout.

### Task 3: Update typed bridge and consumers

**Files:**
- Modify: `container/include/container/typed.h`
- Modify: `container/include/container/meta.h`
- Modify: `tests/containers/*`
- Modify: `Makefile`
- Modify: docs/README references.

**Interfaces:**
- `typed(List, UserList, User)` remains unchanged.
- Generated typed ABI remains `UserList_init`, `UserList_push_back`, etc.

- [x] Update CMeta typed kind registrations to call `CONTAINER_*_DEFINE`.
- [x] Update includes/source lists to `container/...`.
- [x] Run generic/value/container, Range, multi-TU, and CFlow stream tests.

### Task 4: Add CMake container target

**Files:**
- Create: `container/CMakeLists.txt`
- Modify: root `CMakeLists.txt`

**Interfaces:**
- Produces CMake alias `TurboUtils::Container`.

- [x] Build `turbo_container` from explicit raw source list.
- [x] Add public include path `container/include` and C11 feature.
- [x] Export/install as `TurboUtils::Container` / `Container`.
- [x] Install public `container/*.h` headers.
- [x] Add `add_subdirectory(container)` in root CMake before CMeta/CFlow integration consumers.
- [x] Run focused CMake configure/build/install smoke.

### Task 5: Full verification

**Files:** none beyond fixes required by verification.

- [x] Run `make verify`.
- [x] Run `make matrix`.
- [x] Run `make sanitize`.
- [x] Run boundary residue scans for old container module names.
- [x] Review `git diff --stat` and `git diff --check`.
