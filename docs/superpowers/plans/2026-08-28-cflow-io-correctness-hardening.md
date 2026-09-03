# CFlow I/O Correctness Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fail fast when CFlow I/O synchronization primitives cannot be created, preserve exact constructor errors, and make the existing destruction and dependency contracts unambiguous.

**Architecture:** Keep the existing fixed-capacity Actor, native backend, manual Executor, MPSC admission, and exclusive control-plane lifecycle unchanged. Add test-only fault-injection translation units that compile the real implementation with renamed public symbols and controlled dependency failures; production code gains only the missing validation and exact error propagation.

**Tech Stack:** ISO C11, Salts Platform mutexes, CFlow Actor/native I/O, CFlowFS, TinyTest, CMake user presets.

**Spec:** `docs/superpowers/specs/2026-08-26-cflow-async-file-facade-design.md`

## Global Constraints

- Preserve all public function signatures, enum values, ownership transfer, capacity, backpressure, callback, and shutdown behavior.
- Initialization and destruction remain exclusive control-plane operations; accepted submit/cancel remains MPSC.
- No runtime fallback, unbounded allocation, new dependency, or data-path structure change.
- Invalid configuration remains `SALTS_EINVAL`; synchronization allocation failure is `SALTS_ENOMEM`; lower-layer constructor errors retain their original code.
- Use `rg.exe`/`fd.exe` for repository searches and user CMake presets for configure, build, and test.
- Do not create a git commit unless the user separately requests repository-history mutation.

---

### Task 1: Reject a missing file-facade mutex before backend initialization

**Files:**
- Create: `cflow/tests/cflow_io_file_init_failure_test.c`
- Modify: `cflow/tests/CMakeLists.txt`
- Modify: `cflow/src/io_file.c:259`

**Interfaces:**
- Consumes: `salts_mutex_init(salts_mutex_t *)`, `cflow_io_file_open(...)`.
- Produces: unchanged public API; `cflow_io_file_open()` returns `SALTS_ENOMEM` and leaves `file.impl == NULL` when its mutex handle remains null.

- [x] **Step 1: Write the failing real-implementation test**

  Compile `cflow/src/io_file.c` into a test-only translation unit after renaming its public `cflow_io_file_*` definitions. Replace only `salts_mutex_init` with a function that writes a null handle and replace backend initialization with a counting sentinel returning `SALTS_EIO`. Assert that open returns `SALTS_ENOMEM`, does not call the backend sentinel, leaves the public handle zero, and does not create the path.

- [x] **Step 2: Build and run the test to verify RED**

  Run the `win-release-user` configure/build path and `ctest --preset win-release-user -R '^cflow_io_file_test$' --output-on-failure`. Expected before the fix: the backend sentinel is called and/or `SALTS_EIO` is returned.

- [x] **Step 3: Implement the minimal production check**

  Immediately after `salts_mutex_init(&impl->gate)`, add:

  ```c
  if (impl->gate == NULL) {
      free(impl->slots);
      free(impl);
      return SALTS_ENOMEM;
  }
  ```

- [x] **Step 4: Rebuild and rerun the focused test to verify GREEN**

  Expected: the fault-injection test passes and the existing file-facade tests remain green.

### Task 2: Apply the same synchronization invariant to CFlowFS

**Files:**
- Create: `cflow-fs/tests/cflow_fs_init_failure_test.c`
- Modify: `cflow-fs/tests/CMakeLists.txt`
- Modify: `cflow-fs/src/fs.c:322`

**Interfaces:**
- Consumes: `salts_mutex_init(salts_mutex_t *)`, `cflow_fs_service_init(...)`.
- Produces: unchanged public API; initialization returns `SALTS_ENOMEM` without constructing the worker Executor when its mutex handle remains null.

- [x] **Step 1: Write the failing real-implementation test**

  Compile `cflow-fs/src/fs.c` with renamed public `cflow_fs_*` definitions. Inject a null mutex and a counting `cflow_executor_worker_init_with_capacity` sentinel. Assert `SALTS_ENOMEM`, zero service state, and zero Executor-init calls.

- [x] **Step 2: Run the focused CFlowFS test to verify RED**

  Run `ctest --preset win-release-user -R '^cflow_fs_test$' --output-on-failure`. Expected before the fix: the Executor sentinel is called.

- [x] **Step 3: Implement the minimal production check**

  After mutex initialization, release the preallocated path/slot/state storage and return `SALTS_ENOMEM` when `impl->gate == NULL`.

- [x] **Step 4: Rerun the focused test to verify GREEN**

  Expected: the injected failure stops before Executor construction, with no public state publication.

### Task 3: Preserve exact IO Source constructor errors

**Files:**
- Modify: `cflow/src/io_source.c:998`

**Interfaces:**
- Consumes: `cflow_io_actor_init(...)` returning `SALTS_OK`, `SALTS_EINVAL`, or `SALTS_ENOMEM`.
- Produces: the same documented constructor result set without rewriting `SALTS_EINVAL` as `SALTS_ENOMEM`.

- [x] **Step 1: Confirm the existing public contract and call-site preconditions**

  Verify that `cflow/include/cflow/io_source.h` already permits both `SALTS_EINVAL` and `SALTS_ENOMEM`, so exact propagation is source- and ABI-compatible.

- [x] **Step 2: Remove the lossy conversion**

  Replace the nested status rewrite with a direct `goto cleanup` on nonzero Actor initialization status.

- [x] **Step 3: Run `cflow_io_source_test`**

  Verify construction rejection, ownership preservation, source cancellation, and owner close/drain behavior remain unchanged.

### Task 4: Synchronize public lifecycle and target-dependency documentation

**Files:**
- Modify: `cflow/include/cflow/io_file.h:151`
- Modify: `ARCHITECTURE.md:208`
- Modify: `ARCHITECTURE.md:330`

**Interfaces:**
- Consumes: existing exclusive `destroy` contract and current CMake `PUBLIC Salts::Platform` dependency.
- Produces: documentation only; no public ABI or runtime behavior change.

- [x] **Step 1: Clarify exclusive destruction**

  State in `io_file.h` that the driver and all submit/cancel producers must be stopped and joined, and that no facade API entry may race destroy.

- [x] **Step 2: Correct the architecture dependency description and matrix**

  Record `CFlow -> CMeta + Platform` as the current public target dependency, with Concurrency remaining private. Explain that readiness public headers expose Platform types.

- [x] **Step 3: Check installed-header and target references**

  Use repository searches and the existing C/C++ public-header tests to confirm the documentation matches the actual exported target.

### Task 5: Final verification

**Files:**
- Verify all modified files.

**Interfaces:**
- Consumes: Tasks 1-4.
- Produces: reproducible evidence for the complete change set.

- [x] **Step 1: Run formatting and diff checks**

  Run the repository formatter for changed C files if configured, then `git diff --check`.

- [x] **Step 2: Build focused targets**

  Build `cflow_io_file_test`, `cflow_io_source_test`, and `cflow_fs_test` through `win-release-user`.

- [x] **Step 3: Run focused CTest filters**

  Run the three test executables through the matching CTest preset with `--output-on-failure`.

- [x] **Step 4: Expand to adjacent I/O regression tests**

  Run `cflow_io_actor_test`, `cflow_io_native_test`, and `cflow_readiness_test` if the focused set passes.

- [x] **Step 5: Audit final state**

  Run `git status --short`, inspect `git diff`, and report any verification blocker without claiming unexecuted tests passed.
