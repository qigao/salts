# Container and Salts Header Path Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox tracking.

**Goal:** Rename the former STL module and identifiers to `container`, and expose its public headers under `cstl`.

**Architecture:** Preserve behavior while changing repository-local module naming, C identifiers, test names, documentation references, and installed include paths. CMake package and target namespace migration is a separate repository-wide Salts step.

**Tech Stack:** C11/C++17, CMake Presets, Ninja/MSVC, TinyTest, PowerShell, `fd`, and `rg`.

**Spec:** User-approved result: module/API spelling `container`; public include spelling `cstl`.

## Global Constraints

- Work only in `.worktrees/rename-salts` on `refactor/rename-salts`.
- Exclude generated build trees, dependency installs, `.codegraph`, and other worktrees.
- Preserve runtime behavior; this checkpoint changes names and paths only.
- Use `win-release-user` presets and verify install output under the Salts prefix.

---

## Task 1: Prove the new public include is initially absent

- [x] Change `cstl/tests/cstl_header_test.c` to include `<cstl.h>`.
- [x] Build the original header-test target and confirm the failure is specifically the missing new header.

## Task 2: Rename the module and public headers

- [x] Move the former module tree to `cstl/`.
- [x] Move the former public header tree to `cstl/include/salts/`.
- [x] Rename tracked files to use the `container` spelling.
- [x] Replace old case variants with `container`, `Container`, and `CONTAINER`.
- [x] Replace old STL include paths with `cstl`.
- [x] Update root and module CMake paths and install rules.

## Task 3: Verify source, build, tests, and install

- [x] Confirm `rg` and `fd` find no remaining old module/include spellings in tracked source.
- [x] Fresh-configure `win-release-user`.
- [x] Build the renamed focused test and run the focused CTest set.
- [x] Build all targets and run the complete CTest preset.
- [x] Run `install-win-release-user` and confirm the manifest contains `include/cstl.h` and no former STL include entries.
