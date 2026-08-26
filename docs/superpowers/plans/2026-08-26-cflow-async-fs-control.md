# CFlow Bounded Async Filesystem Control Implementation Plan

> **Execution:** Use `superpowers:executing-plans` task-by-task and preserve a
> failing-test observation before each implementation increment.

**Goal:** Deliver issue #112 as an installable `TurboUtils::CFlowFS` target.

**Architecture:** Fixed operation slots copy paths on admission, run existing
`turbo_fs` calls through a bounded CFlow Worker Executor, and deliver terminal
callbacks through one explicit driver.

**Spec:** `docs/superpowers/specs/2026-08-26-cflow-async-fs-control-design.md`

### Task 1: Target and public contract

**Files:** create `cflow-fs/CMakeLists.txt`, `cflow-fs/include/cflow/fs.h`, and
`cflow-fs/tests/cflow_fs_test.c`; modify root `CMakeLists.txt`.

- [x] Add C and C++ compile-contract tests for every public type and signature.
- [x] Configure/build and retain the expected missing-header RED result.
- [x] Add the optional adapter target after `utils` so the existing dependency
  direction remains acyclic.
- [x] Build the public contract GREEN.

### Task 2: Lifecycle, bounded admission, and stat

**Files:** create `cflow-fs/src/fs.c`; extend `cflow_fs_test.c`.

- [x] Add failing tests for config validation, path copying, stat/lstat,
  saturation, callback thread affinity, close, drain, and reusable zero state.
- [x] Allocate checked fixed slot/path storage and initialize a bounded Worker
  Executor without publishing partial state.
- [x] Implement MPSC slot admission, exact status mapping, worker terminal
  phases, single-driver delivery, and cancel-pending shutdown.
- [x] Run focused tests GREEN and Executor regressions.

### Task 3: Directory and path mutation operations

**Files:** extend `cflow-fs/src/fs.c` and `cflow-fs/tests/cflow_fs_test.c`.

- [x] Add RED tests for directory contents, empty directories, entry/name
  overflow, mkdir/rmdir, rename replacement, unlink, missing paths, and reuse.
- [x] Implement directory enumeration into caller-provided bounded storage and
  commit caller counts only after a complete fit.
- [x] Implement mutation operations by delegating to existing `turbo_fs` APIs.
- [x] Run focused and adjacent Core filesystem tests GREEN.

### Task 4: Documentation and verification

**Files:** update CFlowFS README and this checklist.

- [x] Document synchronous Core, worker-backed control plane, and native async
  file data-plane distinctions with a complete lifecycle example.
- [x] Run `git diff --check`, focused Release tests, C/C++ header tests, install
  consumer validation, and expanded CFlow/Core regressions.
- [x] Review checked arithmetic, ownership transfer, callback lifetime, close
  ordering, and every partial-init cleanup path before commit.
