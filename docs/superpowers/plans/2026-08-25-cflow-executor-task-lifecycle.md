# CFlow Executor Task Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add copied Executor task descriptors whose accepted tasks receive exactly one run-or-cancel outcome followed by exactly one finalizer callback.

**Architecture:** Extend the opaque ThreadPool queue entry with optional cancel and finalize callbacks, then adapt CFlow Manual, Serial, and Worker built-ins through additive free functions. Preserve every existing vtable field and legacy `fn/user` entry point.

**Tech Stack:** ISO C11, CMeta interfaces, Salts ThreadPool/Disruptor, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-24-cflow-executor-runtime-protocol-design.md`

## Global Constraints

- Successful admission copies the descriptor; `user` remains borrowed.
- Accepted work follows `run -> finalize` or `cancel -> finalize` exactly once.
- Rejected admission invokes no callback.
- Wait-idle returns only after finalization has completed.
- No lock is held while invoking run, cancel, or finalize.
- Existing vtable layout, function signatures, and legacy entry points remain
  unchanged. Owning constructors now require a zero handle, reject live-handle
  reinitialization, and return the handle to zero after destroy.
- Capacity remains task-slot based and no per-task allocation is added.

---

### Task 1: ThreadPool task descriptor protocol

**Files:**
- Modify: `concurrency/include/salts/thread_pool.h`
- Modify: `concurrency/src/thread_pool.c`
- Test: `concurrency/tests/thread_pool_test.c`

**Interfaces:**
- Produces: `salts_threadpool_task_t`, `salts_threadpool_submit_task`, and `salts_threadpool_try_submit_task`.
- Preserves: `salts_threadpool_submit` and `salts_threadpool_try_submit` as descriptor wrappers.

- [x] **Step 1: Write failing lifecycle tests**

  Add a task probe with independent run/cancel/finalize counters and an ordered
  trace. Verify normal work records run then finalize, queued cancel-pending
  work records cancel then finalize, and rejected work records no callback.

- [x] **Step 2: Run focused RED**

  Build `thread_pool_test`. Expected: compilation fails because the descriptor
  type and submission functions do not exist.

- [x] **Step 3: Implement the minimal descriptor path**

  Copy the descriptor into the bounded queue slot. A worker copies a claimed
  descriptor locally and releases the physical slot before executing hooks
  without locks. Serialize only claim/copy/release because Disruptor reclaims a
  contiguous completion prefix. Keep the logical queue reservation through
  cancellation finalization, keep callback TLS active for all hooks, and update
  the completed/cancelled terminal counter only after finalize returns.

- [x] **Step 4: Run focused GREEN**

  Rebuild and run the descriptor filters, then the complete
  `thread_pool_test` executable.

### Task 2: CFlow built-in task descriptor adapter

**Files:**
- Modify: `cflow/include/cflow/executor.h`
- Modify: `cflow/src/executor.c`
- Test: `cflow/tests/cflow_execution_test.c`

**Interfaces:**
- Produces: `cflow_executor_task`, `cflow_executor_try_post_task`, and
  `cflow_executor_control_post_task`.
- Consumes: Task 1 ThreadPool descriptor entry points.

- [x] **Step 1: Write failing Manual and Worker tests**

  Verify Manual execution and cancellation ordering, Worker cancel-pending
  ordering, and no callbacks after full/closed rejection. Keep existing legacy
  tests unchanged.

- [x] **Step 2: Run focused RED**

  Build `cflow_execution_test`. Expected: compilation fails because the CFlow
  descriptor type and submission functions do not exist.

- [x] **Step 3: Implement built-in adapters**

  Store copied descriptors in Manual fixed storage; invoke terminal hooks under
  Manual callback identity; translate pool descriptors field-by-field to the
  ThreadPool API. Return invalid argument for foreign executor/control vtables.

- [x] **Step 4: Run focused GREEN**

  Rebuild and run the new filters, then the complete
  `cflow_execution_test` executable.

### Task 3: Regression and protocol verification

**Files:**
- Modify: `docs/superpowers/plans/2026-08-25-cflow-executor-task-lifecycle.md`

**Interfaces:**
- Consumes: Tasks 1 and 2.
- Produces: reproducible verification evidence.

- [x] **Step 1: Run adjacent Windows Release tests**

  Run ThreadPool, CFlow execution, Machine Runtime, Scheduler compatibility,
  Parallel Reduce, and filesystem tests through `win-release-user`.

- [x] **Step 2: Run Windows ASan-focused tests**

  Build and run the touched ThreadPool and CFlow test targets with
  `win-dev-user`; if the ASan runtime is unavailable, record that exact blocker.

- [x] **Step 3: Check source quality and diff**

  Run `git diff --check`, scan touched production files for prohibited
  placeholder markers, inspect the diff, and synchronize CodeGraph.

- [x] **Step 4: Record exact evidence**

  Mark completed checkboxes and append commands, exit codes, test counts, and
  any unavailable verification with its residual risk.

## Verification Evidence

- ThreadPool RED: `cmake --build --preset win-release-user --target
  thread_pool_test` failed at the missing `salts_threadpool_task_t`,
  `salts_threadpool_submit_task`, and `salts_threadpool_try_submit_task`
  declarations.
- ThreadPool GREEN: the descriptor filters passed 5/5; the complete executable
  passed 19/19 tests and 120 assertions.
- CFlow RED: `cflow_execution_test` failed at the missing
  `cflow_executor_task`, `cflow_executor_try_post_task`, and
  `cflow_executor_control_post_task` declarations.
- CFlow GREEN: descriptor filters passed 5/5 and 66 assertions; after the
  destroy and observable-settlement cases were added, the complete executable
  passed 25/25 tests and 273 assertions.
- Review regression: a blocking cancellation finalizer initially exposed
  `queued == 0` before cancellation settlement. Keeping the queue reservation
  through `cancel -> finalize` made the focused ThreadPool test pass with 18
  assertions and preserved the CFlow in-flight partition
  `accepted == queued + running + completed + cancelled`.
- Destroy mutation check: replacing `manual_cancel_pending()` with aggregate
  cancellation made the focused test fail (`expected 1 but got 0`); restoring
  the settlement call returned the test to GREEN with 7 assertions.
- Adjacent Release CTest passed 6/6: ThreadPool, CFlow execution, Machine
  Runtime, Scheduler compatibility, Parallel Reduce, and filesystem.
- Windows ASan configure/build/test through `win-dev-user` passed the touched
  ThreadPool and CFlow execution tests 2/2; the ASan runtime was resolved from
  the active VS toolchain environment.
- Final Windows Release build completed and CTest passed 138/138 in 14.84
  seconds, including C and C++ public-header tests.
- `git diff --check` passed; the prohibited placeholder scan had no match;
  CodeGraph synchronized six changed code files.
