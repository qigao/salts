# CFlow Statechart Terminal Adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a tested, allocation-free Statechart terminal Resumable/Source projection that composes with CFlow Run without inventing value-observation semantics.

**Architecture:** A thin adapter borrows the opaque Statechart instance and reuses the existing `cflow_resumable`, `cflow_source`, `cflow_waitable`, and terminal-waker protocols. The instance remains the sole state owner; one attached adapter stores at most one downstream waiter and one terminal waiter under the existing instance mutex.

**Tech Stack:** C11, CMeta interfaces, CFlow Runtime/Source/Executor/Statechart, Salts synchronization, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-27-cflow-statechart-terminal-adapter-design.md`

## Global Constraints

- Preserve existing Statechart semantics and the `CFlow -> CMeta` dependency direction.
- Do not add value observations, state-snapshot emissions, queues, fallback execution, or unbounded allocation.
- Use one borrowed adapter per instance and reject occupied destinations transactionally.
- Never invoke a waker while holding the instance mutex.
- Keep public APIs additive and C11/C++ compatible.

---

### Task 1: Specify adapter behavior with failing TinyTests

**Files:**
- Create: `cflow/tests/cflow_statechart_runtime_adapter_test.c`
- Modify: `cflow/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: existing `cflow_statechart_instance`, `cflow_resumable`, `cflow_source`, `cflow_run`, and test scheduler APIs.
- Produces: compile-time and behavioral requirements for `cflow_statechart_instance_as_terminal_resumable` and `cflow_statechart_instance_as_terminal_source`.

- [x] **Step 1: Add the focused test target and literal fixture**

  Define one compound Statechart with an initial child, active atomic child,
  final child, and one typed external Event. Keep all fixture storage alive
  through instance destruction.

- [x] **Step 2: Add adapter admission and `WAIT -> DONE` tests**

  Assert an occupied output and a second adapter are rejected unchanged,
  `resume` initially returns `WAIT`, arming its waitable succeeds, close wakes
  exactly once, and the next resume returns `DONE` without modifying output.

- [x] **Step 3: Add Source terminal/error and destroy-gate tests**

  Assert Source terminal polling reports OPEN/DONE or OPEN/ERROR, error text is
  the Statechart first error, instance destroy returns WOULD_BLOCK while the
  adapter is attached, and succeeds after adapter destroy.

- [x] **Step 4: Add Run integration test**

  Move the terminal Source into an identity Graph Run, request one item, pump
  until WAIT, close the Statechart, pump the woken Run, and assert zero values,
  one done callback, and no error.

- [x] **Step 5: Configure/build and witness RED**

  Run:

  ```text
  cmake --fresh --preset win-release-user
  cmake --build --preset win-release-user --target cflow_statechart_runtime_adapter_test
  ```

  Expected: compilation fails because the two public adapter functions do not
  exist.

### Task 2: Implement the terminal projection

**Files:**
- Modify: `cflow/include/cflow/statechart_runtime.h`
- Modify: `cflow/src/statechart_runtime.c`

**Interfaces:**
- Consumes: the failing tests from Task 1 and existing CMeta Source/Waitable helpers.
- Produces: the two public attach functions and their borrowed adapter implementation.

- [x] **Step 1: Declare the additive APIs and lifecycle contract**

  Document the empty-value stream, borrowed instance lifetime, single-adapter
  rule, cancel/detach behavior, and required shutdown order in the public
  header.

- [x] **Step 2: Add adapter state under the existing mutex**

  Add `adapter_attached`, `downstream_waiter`, and `terminal_waiter`. Provide
  locked take/clear helpers and invoke copied wakers only after unlocking.

- [x] **Step 3: Implement Resumable and Source interfaces**

  `resume` returns ERROR for the first error, DONE after settled termination,
  otherwise WAIT with a Statechart waitable. Source name/type, terminal bind,
  polling, cancel, and destroy delegate only to this instance.

- [x] **Step 4: Protect instance destruction**

  Return `CFLOW_STATECHART_RUNTIME_WOULD_BLOCK` without clearing the handle if
  an adapter remains attached. Adapter destroy cancels, clears waiters, and
  detaches before instance destruction is permitted.

- [x] **Step 5: Build and verify GREEN**

  Build and run `cflow_statechart_runtime_adapter_test`; all cases must pass
  with no warnings or framework errors.

### Task 3: Public surface and regression verification

**Files:**
- Modify: `cflow/README.md`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`
- Modify: `docs/superpowers/plans/2026-08-27-cflow-statechart-phase1.md`

**Interfaces:**
- Consumes: Task 2's stable public API.
- Produces: documented usage, C++ compile coverage, and corrected Phase 1 completion record.

- [x] **Step 1: Document the terminal-only projection**

  Add a shutdown example and state explicitly that the adapter emits no VALUE;
  observation/value and Actor integration remain tracked by #125.

- [x] **Step 2: Extend aggregate C++ header coverage**

  Compile zero-initialized Resumable/Source handles and references to both new
  function signatures through the aggregate public header.

- [x] **Step 3: Correct the Phase 1 plan record**

  Replace the inaccurate completed Source/Resumable value-observation claim
  with the terminal projection actually delivered here, and leave value
  observation explicitly outside this slice.

- [x] **Step 4: Run focused and adjacent regressions**

  Build the new test plus Statechart runtime, runtime, Machine runtime,
  hierarchy, Actor, and C++ header targets. Run their CTest names with the
  public Windows Release preset.

- [x] **Step 5: Run all CFlow tests and review the diff**

  Run `ctest --preset win-release-user -R "^cflow_" --output-on-failure`, sync
  CodeGraph, inspect affected symbols, confirm `.codegraph/` is untracked, and
  review ownership/error paths before reporting completion.

## Verification evidence (2026-08-27)

- RED: the focused Windows Release target compiled the new test and failed to
  link only the two absent public adapter symbols.
- Windows Release: the focused adapter test passed; seven adjacent Runtime,
  Statechart, Machine, hierarchy, Actor, and C++ header tests passed; the full
  `^cflow_` set passed 32/32.
- Windows ASan: Runtime, Statechart runtime, terminal adapter, and C++ public
  header tests passed 4/4 with AddressSanitizer enabled.
- Installed package smoke verification installed the public header and linked
  all 18 C/C++ consumer targets, including `Salts::CFlow`.
- `codegraph affected` selected the focused adapter test, `.codegraph/` stayed
  ignored, and `git diff --check` passed.
