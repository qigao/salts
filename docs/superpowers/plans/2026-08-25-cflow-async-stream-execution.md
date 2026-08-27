# CFlow Async Stream Execution Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Allow a bound CFlow/TurboSTL Stream to execute asynchronously through an existing concurrent scheduler while preserving serialized Run and transactional Collector semantics.

**Architecture:** Add one opaque Stream execution handle above `cflow_run`. The handle owns normalized Graph/runtime state and a Collector transaction, borrows the scheduler/source/output, and exposes start, wait, cancel, snapshot, and destroy control-plane operations. TurboSTL adds only typed convenience wrappers.

**Tech Stack:** ISO C11, CFlow Run/Scheduler/Range Source, CMeta Collector, TurboUtils thread primitives, TurboSTL typed containers, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-25-cflow-async-stream-execution-design.md`

---

### Task 1: Lock the public contract with failing core tests

**Files:**
- Create: `cflow/tests/cflow_stream_execution_test.c`
- Modify: `cflow/tests/CMakeLists.txt`

1. Add tests for invalid admission, worker execution success, Collector failure, cancellation, callback reentrancy protection, and shared-scheduler independent executions.
2. Add the test target and C11 target properties.
3. Build the target and confirm RED because the new public API is absent.

### Task 2: Implement the CFlow execution handle

**Files:**
- Create: `cflow/include/cflow/stream_execution.h`
- Create: `cflow/src/stream_execution.c`
- Modify: `cflow/include/cflow/cflow.h`
- Modify: `cflow/CMakeLists.txt`

1. Declare the opaque handle, state/status/snapshot types, and lifecycle functions with ownership and threading docs.
2. Implement admission validation, Graph normalization, checked Range source creation, Collector begin, Run open, and demand request with complete rollback.
3. Bridge Run sink callbacks into Collector accept/finish/fail and publish exactly one terminal state under mutex/condition protection.
4. Implement synchronous external cancellation, callback-context WOULD_BLOCK protection, waiting, snapshot, and idempotent ZERO destruction.
5. Build and run `cflow_stream_execution_test` until GREEN.

### Task 3: Expose the TurboSTL convenience surface

**Files:**
- Modify: `turbostl/include/turbostl/stream.h`
- Modify: `turbostl/tests/turbostl_stream_test.c`

1. Add a TurboSTL alias/wrapper and `collect_async[_typed]` macros that create the existing typed Collector and delegate to CFlow.
2. Add a real worker scheduler test for typed List output and verify wait/snapshot/destroy behavior.
3. Build and run `turbostl_stream_test` until GREEN.

### Task 4: Document the execution boundary

**Files:**
- Modify: `cflow/README.md`
- Modify: `turbostl/README.md`

1. Document pipeline-level concurrency, borrowed lifetimes, terminal state inspection, cancellation, and the non-goal of Java compatibility or implicit operator parallelism.
2. Include a complete compiling usage example with cleanup order.

### Task 5: Verify compatibility and integration

**Files:**
- Verify only

1. Run focused CFlow and TurboSTL tests.
2. Build and run the C++ aggregate-header test.
3. Run the repository release preset build and full CTest suite.
4. Inspect `git diff --check`, `git status`, and the final diff for unrelated changes.
