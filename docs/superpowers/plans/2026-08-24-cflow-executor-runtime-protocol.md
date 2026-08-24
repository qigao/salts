# CFlow Executor Runtime Protocol Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the proved Executor admission, callback-context, shutdown, and terminal-accounting protocol in the C Manual, Serial, and Worker backends.

**Architecture:** Keep the existing `cflow_executor` ABI as the data plane and add an optional `cflow_executor_control` view over the same built-in backend state. Add policy-aware shutdown and callback-context fail-fast primitives to `turbo_threadpool`, then adapt Manual and pool executors to one public protocol vocabulary.

**Tech Stack:** ISO C11, CMeta interfaces, TurboUtils ThreadPool/Disruptor, TinyTest, CMake Presets, Lean protocol specification.

**Spec:** `docs/superpowers/specs/2026-08-24-cflow-executor-runtime-protocol-design.md`

## Global Constraints

- Preserve the `cflow_executor` vtable layout and existing constructor signatures.
- Capacity bounds queued work only; running work is accounted separately.
- Never hold a lock while invoking a task callback.
- Same-executor callback blocking operations fail fast.
- Existing shutdown remains drain; cancel-pending is opt-in through the control view.
- Task payloads remain borrowed and are never destroyed by Executor cancellation.
- Claim no unconditional OS fairness or task termination.

---

### Task 1: ThreadPool callback and shutdown protocol

**Files:**
- Modify: `concurrency/include/turbo/thread_pool.h`
- Modify: `concurrency/src/thread_pool.c`
- Test: `concurrency/tests/thread_pool_test.c`

**Interfaces:**
- Produces: `turbo_threadpool_shutdown_policy_t`, `turbo_threadpool_wait_status`, `turbo_threadpool_shutdown_with_policy`, and cancelled-task observation.
- Preserves: `turbo_threadpool_shutdown` as drain and `turbo_threadpool_wait` as the compatibility wrapper.

- [x] **Step 1: Write callback self-post/self-wait and cancel-pending tests**

  Add a one-worker/one-slot callback test whose first nested submit fills the
  queue and whose second blocking submit returns `TURBO_EBUSY`; assert
  status-returning wait also returns `TURBO_EBUSY`. Add a gated running task plus
  two queued tasks, invoke cancel-pending shutdown, release the gate, and assert
  one completion, two cancellations, and zero pending.

- [x] **Step 2: Run focused RED**

  Build `thread_pool_test` and run its new filters. Expected failure is missing
  policy/wait/cancellation symbols; no production behavior is changed yet.

- [x] **Step 3: Implement the minimal pool transitions**

  Track the current callback pool in C11 thread-local storage. Reuse
  non-blocking reservation for a same-pool blocking submit and return
  `TURBO_EBUSY` only when it cannot proceed. Add a cancel-pending flag and
  cancellation count; workers settle claimed queued entries as cancelled after
  cancel shutdown and continue executing already-running callbacks.

- [x] **Step 4: Run focused GREEN**

  Rebuild and run `thread_pool_test`; both new behaviors and all existing pool
  tests must pass.

### Task 2: Add the CFlow protocol control view

**Files:**
- Modify: `cflow/include/cflow/executor.h`
- Modify: `cflow/src/executor.c`
- Test: `cflow/tests/cflow_execution_test.c`

**Interfaces:**
- Produces: `cflow_executor_control`, lifecycle/policy/post/wait enums,
  `cflow_executor_protocol_stats`, and `cflow_executor_as_control`.
- Consumes: Task 1 status-returning wait, shutdown policy, and pool accounting.

- [x] **Step 1: Write protocol-view RED tests**

  Assert built-in executors expose the view; a foreign test implementation does
  not. Verify Manual drain keeps queued work runnable, Manual cancel settles it
  without invocation, pool callback self-wait/self-post return would-block, and
  terminal accepted equals completed plus cancelled.

- [x] **Step 2: Run focused RED**

  Build `cflow_execution_test` and run the protocol filters. Expected failure is
  missing control types and conversion function.

- [x] **Step 3: Implement Manual and pool adapters**

  Add lifecycle and accounting to both built-in states. Existing methods call
  the same transition helpers with legacy drain semantics. Define two control
  vtables over the existing state pointers and make conversion recognize only
  the repository-owned built-in vtables.

- [x] **Step 4: Run focused GREEN**

  Rebuild and run `cflow_execution_test`; protocol tests and all legacy
  execution tests must pass.

### Task 3: Refinement and regression verification

**Files:**
- Modify: `docs/superpowers/plans/2026-08-24-cflow-executor-runtime-protocol.md`

**Interfaces:**
- Consumes: Tasks 1 and 2.
- Produces: reproducible evidence attached to this plan.

- [x] **Step 1: Run related C tests**

  Run `thread_pool_test`, `cflow_execution_test`, and CTest filters for CFlow
  machine/runtime/scheduler users of Executor.

- [x] **Step 2: Run formal and full regression tests**

  Run the Lean Executor protocol tests, then the complete Windows Release build
  and test preset.

- [x] **Step 3: Check source quality and diff**

  Scan touched production files for placeholder markers, run
  `git diff --check`, inspect `git diff --stat`, and synchronize CodeGraph.

- [x] **Step 4: Record exact evidence**

  Mark completed checkboxes and append commands, exit codes, and test counts.

## Verification Evidence

- ThreadPool RED: `thread_pool_test.c` failed to compile because
  `turbo_threadpool_wait_status`, policy-aware shutdown, cancellation count,
  and `TURBO_THREADPOOL_SHUTDOWN_CANCEL_PENDING` did not exist.
- ThreadPool focused GREEN: callback self-block and cancel-pending filters each
  passed; the complete executable passed 14 tests and 55 assertions.
- CFlow RED: `cflow_execution_test.c` failed to compile because the control
  interface, protocol enums, and snapshot did not exist.
- CFlow focused GREEN: Manual drain, Manual cancel, Manual/Worker callback
  self-block, and Worker terminal conservation filters passed. A mutation that
  removed Manual full/self-callback detection made the dedicated test fail with
  FULL instead of WOULD_BLOCK; restoring the branch returned it to GREEN.
- Adjacent CTest: ThreadPool, CFlow execution, Machine Runtime, Machine
  Hierarchy, Scheduler compatibility, and Parallel Reduce passed 6/6.
- Formal refinement specification: focused Lean proof/test builds completed 12
  and 13 jobs; `lake test` exited 0.
- Full Windows Release verification: the build preset completed, including C
  and C++ public-header tests; CTest passed 138/138 in 15.36 seconds.
- CodeGraph synchronized six changed C files and identified
  `cflow/tests/cflow_execution_test.c` as the directly affected indexed test.
- Placeholder/proof-escape scan and `git diff --check` completed without a
  relevant match or whitespace error.
